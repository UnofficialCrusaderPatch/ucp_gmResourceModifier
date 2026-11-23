# Gm Resource Modifier

**Autor**: [TheRedDaemon](https://github.com/TheRedDaemon/ucp_gmResourceModifier)

Este módulo permite modificar los recursos GM1 ya cargados por el juego. Crusader trata los archivos GM de forma distinta a los TGX: los TGX se leen del disco cuando hacen falta, mientras que los GM se cargan al inicio. Por eso no basta con interceptar accesos y cambiar rutas. Se identificaron las estructuras de control de los archivos GM en memoria para poder sustituirlos cuando se necesiten.

Se pueden reemplazar archivos completos o imágenes individuales, siempre del mismo tipo, ya que SHC almacena distintos formatos en GM1. El módulo también puede crear, a partir de una imagen, un recurso de imagen única de tipo «interface». El conversor llama a funciones de Windows, por lo que el soporte depende del sistema operativo y de lo que permita Wine.

El módulo no ofrece funciones de juego directamente utilizables, sino una base de bajo nivel. Consulta el README del repositorio para más información.
