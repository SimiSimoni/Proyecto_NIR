import argparse
import json
import math
import time
from dataclasses import dataclass, asdict
from typing import List, Optional

from scholarly import scholarly, ProxyGenerator

try:
    import requests
    REQUESTS_AVAILABLE = True
except ImportError:
    REQUESTS_AVAILABLE = False

try:
    from sentence_transformers import SentenceTransformer, util
    EMBEDDINGS_AVAILABLE = True
except ImportError:
    EMBEDDINGS_AVAILABLE = False


# modelo de datos

@dataclass
class Article:
    title: str
    authors: List[str]
    year: Optional[int]
    citations: int
    abstract: str
    url: str
    venue: str = ""
    source: str = ""                      # "google_scholar" o "semantic_scholar"
    native_rank: Optional[int] = None

    # scores individuales (0-1) y score final, se llenan al rankear
    rank_score: float = 0.0
    citation_score: float = 0.0
    recency_score: float = 0.0
    semantic_score: float = 0.0
    relevance_score: float = 0.0


# scrapers 
#
# Ambas clases exponen el mismo método search(keywords, max_results, year_low,
# year_high, delay) -> List[Article], así que se pueden usar indistintamente
# desde main() y desde el fallback automático.

class GoogleScholarScraper:
    """Este es el mejor scrapper pero Google Scholar puede bloquear la IP o pedir captcha."""

    SOURCE_NAME = "google_scholar"

    def __init__(self, use_free_proxy: bool = False):
        if use_free_proxy:
            self._setup_proxy()

    def _setup_proxy(self):
        """este proxy es para reducir el riesgo de bloqueo de Google Scholar"""
        pg = ProxyGenerator()
        success = pg.FreeProxies()
        if success:
            scholarly.use_proxy(pg)
            print("Proxy configurado correctamente.")
        else:
            print("No se pudo configurar un proxy (mayor riesgo de captcha).")

    def search(self, keywords: str, max_results: int = 20,
               year_low: Optional[int] = None, year_high: Optional[int] = None,
               delay: float = 2.0) -> List[Article]:
        """Busca artículos en Google Scholar y devuelve una lista de Article."""
        print(f"Buscando en Google Scholar: '{keywords}'...")

        query = scholarly.search_pubs(
            keywords,
            year_low=year_low,
            year_high=year_high,
            sort_by="relevance",
        )

        results: List[Article] = []
        for i in range(max_results):
            try:
                pub = next(query)
            except StopIteration:
                break
            except Exception as e:
                # Google Scholar aveces pide un captcha asi que se detiene con lo que ya se obtuvo en vez de tirar toda la búsqueda.
                print(f"Aviso: se detuvo la búsqueda en el resultado {i} ({e}).")
                break

            bib = pub.get("bib", {})
            authors = bib.get("author", [])
            if isinstance(authors, str):
                authors = [authors]

            article = Article(
                title=bib.get("title", "Sin título"),
                authors=authors,
                year=self._safe_int(bib.get("pub_year")),
                citations=pub.get("num_citations", 0) or 0,
                abstract=bib.get("abstract", ""),
                url=pub.get("pub_url") or pub.get("eprint_url", ""),
                venue=bib.get("venue", ""),
                native_rank=pub.get("gsrank"),
                source=self.SOURCE_NAME,
            )
            results.append(article)
            print(f"  [{i + 1}/{max_results}] ({article.citations:>4} citas) {article.title[:65]}")

            time.sleep(delay)  # para no golpear el servidor demasiado rápido

        print(f"Se obtuvieron {len(results)} artículos (Google Scholar).")
        return results

    @staticmethod
    def _safe_int(value) -> Optional[int]:
        try:
            return int(value)
        except (TypeError, ValueError):
            return None


class SemanticScholarScraper:
    """Alternativa con la API pública de Semantic Scholar. Más lenta y con peor ranking nativo, pero no bloquea la IP."""
    SOURCE_NAME = "semantic_scholar"
    API_URL = "https://api.semanticscholar.org/graph/v1/paper/search"
    FIELDS = "title,authors,year,citationCount,abstract,url,venue"

    def __init__(self, api_key: Optional[str] = None):
        if not REQUESTS_AVAILABLE:
            raise RuntimeError(
                "El paquete 'requests' no está instalado. Ejecuta: pip install requests"
            )
        self.session = requests.Session()
        if api_key:
            self.session.headers["x-api-key"] = api_key

    def search(self, keywords: str, max_results: int = 20,
               year_low: Optional[int] = None, year_high: Optional[int] = None,
               delay: float = 1.0) -> List[Article]:
        """Busca artículos en Semantic Scholar y devuelve una lista de Article."""
        print(f"Buscando en Semantic Scholar: '{keywords}'...")

        params_base = {"query": keywords, "fields": self.FIELDS}
        year_param = self._build_year_param(year_low, year_high)
        if year_param:
            params_base["year"] = year_param

        results: List[Article] = []
        offset = 0

        while len(results) < max_results:
            limit = min(100, max_results - len(results))  # la API exige limit <= 100
            params = {**params_base, "limit": limit, "offset": offset}

            data = self._request_with_retry(params)
            if not data:
                break

            papers = data.get("data") or []
            if not papers:
                break

            for j, paper in enumerate(papers):
                authors = [a.get("name", "") for a in (paper.get("authors") or [])]
                article = Article(
                    title=paper.get("title") or "Sin título",
                    authors=authors,
                    year=paper.get("year"),
                    citations=paper.get("citationCount") or 0,
                    abstract=paper.get("abstract") or "",
                    url=paper.get("url") or "",
                    venue=paper.get("venue") or "",
                    native_rank=offset + j + 1,
                    source=self.SOURCE_NAME,
                )
                results.append(article)
                print(f"  [{len(results)}/{max_results}] ({article.citations:>4} citas) {article.title[:65]}")
                if len(results) >= max_results:
                    break

            offset += len(papers)
            if data.get("next") is None or len(papers) < limit:
                break  # no hay más páginas

            time.sleep(delay)

        print(f"Se obtuvieron {len(results)} artículos (Semantic Scholar).")
        return results

    @staticmethod
    def _build_year_param(year_low: Optional[int], year_high: Optional[int]) -> str:
        if year_low and year_high:
            return f"{year_low}-{year_high}"
        if year_low:
            return f"{year_low}-"
        if year_high:
            return f"-{year_high}"
        return ""

    def _request_with_retry(self, params: dict, max_retries: int = 5) -> Optional[dict]:
        wait = 2.0
        for _ in range(max_retries):
            try:
                resp = self.session.get(self.API_URL, params=params, timeout=15)
            except requests.RequestException as e:
                print(f"Aviso: error de red ({e}), reintentando en {wait:.0f}s...")
                time.sleep(wait)
                wait *= 2
                continue

            if resp.status_code == 200:
                return resp.json()

            if resp.status_code == 429:
                print(f"Aviso: límite de tasa de Semantic Scholar alcanzado, esperando {wait:.0f}s...")
                time.sleep(wait)
                wait *= 2
                continue

            print(f"Aviso: Semantic Scholar devolvió status {resp.status_code}: {resp.text[:200]}")
            return None

        print("Aviso: se agotaron los reintentos ante el límite de tasa de Semantic Scholar.")
        return None


SOURCES = {
    GoogleScholarScraper.SOURCE_NAME: GoogleScholarScraper,
    SemanticScholarScraper.SOURCE_NAME: SemanticScholarScraper,
}


def search_with_fallback(source: str, keywords: str, max_results: int,
                          year_low: Optional[int], year_high: Optional[int],
                          delay: float, use_proxy: bool,
                          semantic_api_key: Optional[str]) -> List[Article]:
    """
    source="auto": intenta Google Scholar primero (mejor ranking nativo y cobertura);
    si falla o Google bloquea la búsqueda (0 resultados), usa       
    Semantic Scholar sin que el usuario tenga que reintentar a mano.    
    """
    if source in ("google_scholar", "auto"):
        gs = GoogleScholarScraper(use_free_proxy=use_proxy)
        try:
            articles = gs.search(keywords, max_results=max_results,
                                  year_low=year_low, year_high=year_high, delay=delay)
        except Exception as e:
            print(f"Aviso: Google Scholar falló por completo ({e}).")
            articles = []

        if articles or source == "google_scholar":
            return articles

        print("Google Scholar no devolvió resultados (probable bloqueo de IP/captcha). "
              "Cambiando automáticamente a Semantic Scholar...")

    ss = SemanticScholarScraper(api_key=semantic_api_key)
    return ss.search(keywords, max_results=max_results,
                      year_low=year_low, year_high=year_high,
                      delay=max(delay, 1.0))


# ranking de relevancia 

class RelevanceRanker:

    def __init__(self, embedding_model: str = "all-MiniLM-L6-v2", use_embeddings: bool = True):
        self.embedder = None
        if use_embeddings and EMBEDDINGS_AVAILABLE:
            print(f"Cargando modelo de embeddings '{embedding_model}' (PyTorch)...")
            self.embedder = SentenceTransformer(embedding_model)
        elif use_embeddings and not EMBEDDINGS_AVAILABLE:
            print("Aviso: sentence-transformers no está instalado,"
                  "para instalar ejecuta: pip install sentence-transformers")

    def rank_by_relevance(
        self,
        articles: List[Article],
        query: str,
        w_rank: float = 0.20,
        w_citations: float = 0.35,
        w_recency: float = 0.15,
        w_semantic: float = 0.30,
    ) -> List[Article]:
        """
        Este modelo es un prototipo, se puede ajustar. Que tan relevante es se calcula usando
        rank_score que es el orden que la fuente (Google Scholar o Semantic Scholar) ya devolvió
        citation_score que es el número de citas
        recency_score que es el año de publicación y
        semantic_score que es la similitud semántica entre la query y el título + abstract del artículo.

        Los pesos (w_*) deben sumar ~1.0; cuando por x razon no hay embeddings disponibles
        el peso se redistribuye automáticamente entre citas y el rank score
        """
        if not articles:
            return articles

        # Rank score (nativo de la fuente)
        max_rank = max((a.native_rank or i + 1) for i, a in enumerate(articles))
        for a in articles:
            rank = a.native_rank if a.native_rank else max_rank
            a.rank_score = 1.0 - (rank - 1) / max(max_rank - 1, 1)

        # Citation score
        max_log_citations = max(math.log1p(a.citations) for a in articles) or 1.0
        for a in articles:
            a.citation_score = math.log1p(a.citations) / max_log_citations

        # Recency score
        years = [a.year for a in articles if a.year]
        if years:
            min_year, max_year = min(years), max(years)
            year_range = max(max_year - min_year, 1)
            for a in articles:
                a.recency_score = (a.year - min_year) / year_range if a.year else 0.5
        else:
            for a in articles:
                a.recency_score = 0.5

        # Semantic score
        if self.embedder:
            query_emb = self.embedder.encode(query, convert_to_tensor=True)
            texts = [f"{a.title}. {a.abstract}".strip() for a in articles]
            text_embs = self.embedder.encode(texts, convert_to_tensor=True)
            sims = util.cos_sim(query_emb, text_embs)[0]
            for a, sim in zip(articles, sims):
                a.semantic_score = max(float(sim), 0.0)
        else:
            # cuando no hay embeddings disponibles
            w_citations += w_semantic * 0.6
            w_rank += w_semantic * 0.4
            w_semantic = 0.0

        # score final
        total_w = w_rank + w_citations + w_recency + w_semantic
        for a in articles:
            a.relevance_score = (
                w_rank * a.rank_score
                + w_citations * a.citation_score
                + w_recency * a.recency_score
                + w_semantic * a.semantic_score
            ) / total_w

        return sorted(articles, key=lambda a: a.relevance_score, reverse=True)


# salida

def print_results(articles: List[Article], top_n: int):
    print("\n" + "=" * 88)
    print(f"TOP {min(top_n, len(articles))} ARTÍCULOS MÁS RELEVANTES")
    print("=" * 88)
    for i, a in enumerate(articles[:top_n], 1):
        authors_str = ", ".join(a.authors[:3]) + (" et al." if len(a.authors) > 3 else "")
        print(f"\n{i}. {a.title}")
        print(f"   Fuente: {a.source}")
        print(f"   Autores: {authors_str or 'N/D'}")
        print(f"   Año: {a.year or 'N/D'}  |  Citas: {a.citations}  |  Venue: {a.venue or 'N/D'}")
        print(f"   Score total: {a.relevance_score:.3f}  "
              f"[rank={a.rank_score:.2f}  citas={a.citation_score:.2f}  "
              f"recencia={a.recency_score:.2f}  semántica={a.semantic_score:.2f}]")
        print(f"   URL: {a.url or 'N/D'}")


def save_results(articles: List[Article], filename: str):
    with open(filename, "w", encoding="utf-8") as f:
        json.dump([asdict(a) for a in articles], f, ensure_ascii=False, indent=2)
    print(f"\nResultados completos guardados en: {filename}")


# CLI

def main():
    parser = argparse.ArgumentParser(
        description="Scraper de Google Scholar / Semantic Scholar con ranking de relevancia por citas + semántica.")
    parser.add_argument("keywords", type=str, help="Palabras clave de búsqueda")
    parser.add_argument("--source", choices=["auto", "google_scholar", "semantic_scholar"], default="auto",
                         help="Fuente de datos. 'auto' (default) intenta Google Scholar y cae a Semantic "
                              "Scholar si Google bloquea la búsqueda. 'semantic_scholar' usa solo la API "
                              "oficial (recomendado si tu IP ya está bloqueada por Google).")
    parser.add_argument("--semantic-api-key", type=str, default=None,
                         help="API key de Semantic Scholar (opcional; sin ella hay un límite de tasa "
                              "compartido entre todos los usuarios anónimos)")
    parser.add_argument("--max-results", type=int, default=20, help="Máximo de resultados a extraer")
    parser.add_argument("--top-n", type=int, default=10, help="Cuántos artículos mostrar en el ranking final")
    parser.add_argument("--year-low", type=int, default=None, help="Filtrar desde este año")
    parser.add_argument("--year-high", type=int, default=None, help="Filtrar hasta este año")
    parser.add_argument("--proxy", action="store_true",
                         help="Usar proxy gratuito con Google Scholar (recomendado para evitar bloqueos)")
    parser.add_argument("--delay", type=float, default=2.0, help="Segundos de espera entre requests")
    parser.add_argument("--output", type=str, default="resultados_scholar.json", help="Archivo JSON de salida")
    parser.add_argument("--no-embeddings", action="store_true", help="Desactivar similitud semántica (más rápido, no requiere PyTorch)")
    parser.add_argument("--w-rank", type=float, default=0.20, help="Peso del ranking nativo de la fuente")
    parser.add_argument("--w-citations", type=float, default=0.35, help="Peso del número de citas")
    parser.add_argument("--w-recency", type=float, default=0.15, help="Peso de la recencia (año de publicación)")
    parser.add_argument("--w-semantic", type=float, default=0.30, help="Peso de la similitud semántica")
    args = parser.parse_args()

    articles = search_with_fallback(
        source=args.source,
        keywords=args.keywords,
        max_results=args.max_results,
        year_low=args.year_low,
        year_high=args.year_high,
        delay=args.delay,
        use_proxy=args.proxy,
        semantic_api_key=args.semantic_api_key,
    )

    if not articles:
        print("No se encontraron artículos. Si usaste --source google_scholar, prueba con "
              "--source semantic_scholar o --source auto para evitar el bloqueo de IP.")
        return

    ranker = RelevanceRanker(use_embeddings=not args.no_embeddings)
    ranked = ranker.rank_by_relevance(
        articles,
        query=args.keywords,
        w_rank=args.w_rank,
        w_citations=args.w_citations,
        w_recency=args.w_recency,
        w_semantic=args.w_semantic,
    )

    print_results(ranked, top_n=args.top_n)
    save_results(ranked, args.output)


if __name__ == "__main__":
    main()