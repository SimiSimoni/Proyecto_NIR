import time
from picamera import PiCamera

def probar_camara():
    print("Inicializando la cámara Raspberry Pi NoIR...")
    
    # Instanciar la cámara
    camera = PiCamera()
    
    # Configurar resolución (puedes ajustarla)
    camera.resolution = (1280, 720)
    
    # Opcional: Ajustes típicos para cámaras NoIR (sin filtro infrarrojo)
    camera.framerate = 30
    
    # Dar tiempo a que el sensor de la cámara se ajuste a la luz ambiental
    print("Calibrando exposición... Espera 2 segundos.")
    time.sleep(2)
    
    # Nombre del archivo de prueba
    nombre_archivo = "prueba_noir.jpg"
    
    # Capturar la imagen
    print(f"Tomando fotografía y guardando como '{nombre_archivo}'...")
    camera.capture(nombre_archivo)
    
    # Cerrar la cámara correctamente
    camera.close()
    
    print("¡Prueba exitosa! Revisa tu directorio actual para ver la imagen.")

if __name__ == "__main__":
    probar_camara()
