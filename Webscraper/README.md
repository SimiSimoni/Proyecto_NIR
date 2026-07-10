Busca artículos académicos por palabras clave y los ordena por relevancia combinando el ranking nativo de la fuente, 
el número de citas, que tan reciente es y opcionalmente similitud semántica con embeddings.

Soporta:

- **Google Scholar** 
    Utilizando scholarly, tiene mejor precision de relevancia pero Google puede llegar a bloquear la IP o pedir captcha.
- **Semantic Scholar** 
    API oficial gratuita, esta nunca se bloquea por lo que es la alternativa automatica si Google ya te bloqueó la IP.

`--source auto` intenta Google Scholar primero y, si existe algun error (ya sea por bloqueo o captcha), usa automáticamente 
Semantic Scholar sin intervención manual.

## Instalación
```bash
pip install scholarly requests
pip install sentence-transformers
```

`sentence-transformers` es opcional pero mejora la relevancia utilizando embeddings

## Uso 

```bash
py "C:\Users\Simone\AppData\Local\Programs\Python\Python313\webscraper.py" "tema" --source auto
```

Remplazar el directorio con la ubicacion del archivo y el tema con las keywords a buscar

Las fuentes son 

```bash
# Si tu IP ya esta bloqueada por Google
--source semantic_scholar

# Forzar Google Scholar (sin fallback)
--source google_scholar

# Intenta Google primero, usa Semantic Scholar si bloquea (esta es la default)
--source auto

## Opciones de la CLI

| Opción | Default | Descripción |
|---|---|---|
| `keywords` | — | Palabras clave de búsqueda (posicional, obligatorio) |
| `--source` | `auto` | `auto`, `google_scholar` o `semantic_scholar` |
| `--semantic-api-key` | `None` | API key de Semantic Scholar (opcional, sube el límite de tasa) |
| `--max-results` | `20` | Máximo de resultados a extraer |
| `--top-n` | `10` | Cuántos artículos mostrar en el ranking impreso |
| `--year-low` | `None` | Filtrar desde este año |
| `--year-high` | `None` | Filtrar hasta este año |
| `--proxy` | `False` | Usar proxy gratuito con Google Scholar (reduce riesgo de bloqueo) |
| `--delay` | `2.0` | Segundos de espera entre requests |
| `--output` | `resultados_scholar.json` | Archivo JSON de salida |
| `--no-embeddings` | `False` | Desactivar similitud semántica (más rápido, no requiere PyTorch) |
| `--w-rank` | `0.20` | Peso del ranking nativo de la fuente |
| `--w-citations` | `0.35` | Peso del número de citas |
| `--w-recency` | `0.15` | Peso de la recencia (año de publicación) |
| `--w-semantic` | `0.30` | Peso de la similitud semántica |


## ranking

Cada artículo recibe cuatro calificaciones entre 0 y 1:

- **rank_score**: basado en el orden en que la fuente (Google Scholar o Semantic
  Scholar) ya devolvió el resultado.
- **citation_score**: número de citas, se normalizo con `log1p` para no dejar que
un solo paper con demasiadas citas aplaste al resto.
- **recency_score**: qué tan reciente es el año de publicación relativo al resto
  de los resultados obtenidos.
- **semantic_score**: similitud coseno entre los embeddings de la query y del
  texto (título + abstract) del artículo. Requiere `sentence-transformers`.

El relevance_score final es el promedio ponderado de los cuatro, si no hay embeddings disponibles, el peso de 
se redistribuye automáticamente 


## salida

- **Consola**: top N artículos con autores, año, citas, venue, URL y el desglose de puntajes.
- **JSON** (`--output`): lista completa de artículos con todos los campos,
  incluyendo `source` (de qué fuente vino cada uno) y puntajes.

## Notas

- No todos los papers tienen abstract disponible en Semantic Scholar
- El conteo de citas de Semantic Scholar puede diferir un poco del de Google Scholar.

