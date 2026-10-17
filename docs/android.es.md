# Wind Waker HD en Android

[English](android.md) · [Português](android.pt.md) · **Español**

The Wind Waker HD funciona de forma nativa en dispositivos Android. No es un emulador: el
propio código del juego se recompila para procesadores ARM y dibuja con Vulkan. Necesitas tu propia
copia del juego de Wii U; este proyecto nunca proporciona archivos del juego.

## Qué hace la versión de Android

| | |
|---|---|
| **Controles en pantalla** | Los dos sticks, la cruceta, A B X Y, L R ZL ZR y + −. El botón 🎮 debajo del botón de vista (esquina superior izquierda) los muestra u oculta. Se ocultan mientras hay un mando conectado y vuelven con el siguiente toque. Un mando Bluetooth o USB también funciona. Mantén pulsado 🎮 para editar la distribución táctil: arrastra un control, selecciónalo y usa Tamaño −/+ para cambiar su tamaño; después pulsa Listo. Restablecer recupera la distribución original. Las posiciones y los tamaños se guardan en este dispositivo y se ajustan a la pantalla al girarla. Al editar se sueltan los controles pulsados y no se envían toques al juego. |
| **Vibración del teléfono** | En Controls → Phone vibration, **Touch only** es la opción predeterminada: el teléfono vibra con el juego al usar los controles táctiles. La entrada de un mando o teclado detiene la vibración del teléfono; al usar los controles táctiles se vuelve a seleccionar el teléfono. **Touch or no rumble controller** también hace vibrar el teléfono si ningún mando conectado tiene vibración. **Off** desactiva la vibración del teléfono; **Rumble** silencia tanto el teléfono como los mandos. Se respetan los ajustes de vibración y el interruptor de respuesta táctil de Android. `WWHD_PHONE_RUMBLE=0`, `touch` o `fallback` tiene prioridad sobre la opción guardada al iniciar. |
| **La pantalla del GamePad al pausar** | Pulsa + y la imagen cambia a la pantalla del GamePad (mapa y objetos), como el menú de pausa de la versión de GameCube. Pulsa + otra vez para volver. |
| **60 fps sin que el juego vaya lento** | En los dispositivos Android rápidos el juego muestra 60 imágenes por segundo. Cuando el dispositivo Android se calienta, pasa solo a 30 estables, y el juego nunca se ralentiza. |
| **Dispositivos Android antiguos** | Si el driver de tu GPU Adreno es demasiado antiguo, la pantalla de error ofrece instalar un driver como Mesa Turnip. |
| **Recuperación de la GPU** | Tras un error de dispositivo perdido de la GPU, el siguiente inicio muestra el driver y ofrece el driver del sistema, otro instalado o el selector de ZIP existente antes de iniciar Vulkan. Continuar conserva tu elección; una prueba inicial incompleta sigue usando el driver del sistema. |
| **Partidas guardadas** | Exporta e importa tus partidas y guárdalas en la copia de seguridad de Android. Las partidas de una Wii U real (de EE. UU. o de Europa) también funcionan. |

Android solicita 60 Hz para 30 fps, 60 fps interpolados y 60 fps reales, o la cadencia seleccionada de 120/240 Hz para una interpolación más rápida. Android puede elegir otra frecuencia compatible. La solicitud se repite al volver al juego. Para dejar la elección de frecuencia completamente en manos de Android, establece `WWHD_NO_60HZ=1` en una compilación de desarrollo (`0` mantiene la solicitud activada).

## Teléfonos probados

Medido con el medidor del propio juego en la zona más exigente de la Isla Initia (unas 3900
llamadas de dibujo por imagen). *Velocidad del juego* son los pasos de lógica del juego por
segundo: 30 es la velocidad completa.

| Dispositivo | Procesador / GPU | Imágenes por segundo | Velocidad del juego | Estado |
|---|---|---|---|---|
| Galaxy S25 Ultra | Snapdragon 8 Elite, Adreno 830 | unos 59 fps | 29,7 / 30 | excelente |
| Galaxy Z Fold 8 | SM8850, Adreno 840 | 60 fps, 30 cuando se calienta | 26–30 / 30 | excelente, en las dos pantallas |
| Lenovo Legion Tab | Snapdragon 8 Gen 3, Adreno 750 | 30 fps estables | 28,7–30 / 30 | bueno |
| OnePlus 8 Pro | Snapdragon 865, Adreno 650 (Mesa Turnip) | 30 fps | 26–30 / 30 | funciona; un cierre de la GPU todavía en estudio |
| Galaxy Tab S6 Lite | Snapdragon 720G, Adreno 618 (Mesa Turnip), 4 GB | 15–24 fps | 15–24 / 30 | funciona lento: más o menos a media velocidad en Outset |

Necesitas un dispositivo Android de 64 bits con Android 13 o posterior y Vulkan 1.3 (o un driver
personalizado en Adreno), y unos pocos GB libres.

## Apuntar con el giroscopio del teléfono

Abre **Settings > Controls > Gyro** y elige **Device gyro (phone/tablet)**.
Está desactivado de forma predeterminada y necesita giroscopio y acelerómetro.
Deja el teléfono quieto un segundo para calibrarlo. Activa **Options > Gyro** en
el juego y apunta en primera persona sin mover el stick derecho. Los ejes siguen
la pantalla en ambas orientaciones horizontales. Funciona con controles táctiles
o con mando. Los sensores se detienen en segundo plano y mientras los ajustes
bloquean el juego; al volver, deja el teléfono quieto otra vez. La sensibilidad,
la inversión y la recalibración se explican en la [guía del giroscopio](gyro.md).
La línea de estado indica si faltan sensores.

## Cómo jugar

### La app de instalación (sin PC)

La app de instalación viene en todas las versiones oficiales: descarga el `WindWakerHD-<versión>-android-arm64.apk`
en la página de [Releases](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/releases) (el APK `android-x86_64` es para emuladores y Chromebooks). No lleva
código del juego: prepara el juego en tu dispositivo Android a partir de tu propia copia. Las versiones siguientes
se instalan encima y conservan tus partidas y tus ajustes (el dispositivo Android vuelve a preparar el juego
cuando una actualización lo necesita).

1. Instala el APK. Si ya tienes una versión preparada en tu PC, exporta antes tu partida (mantén
   pulsado el icono de la app) y desinstala esa versión: las dos están firmadas de forma distinta.
2. Abre la app, pulsa **Elegir mi juego** y elige lo que tengas:
   - **Carpeta del juego extraída**: pon el juego en una carpeta propia, por ejemplo `Download/WindWakerHD`,
     y elige esa carpeta (la que contiene `code`, `content` y `meta`). El selector de carpetas se abre en
     Download. Android no permite elegir la carpeta Download en sí, la raíz del almacenamiento interno ni
     `Android/data`, así que el juego necesita una carpeta propia. Si eliges la carpeta justo encima del
     juego, la app encuentra el juego dentro; si eliges una carpeta equivocada, te dice cuál elegir antes
     de copiar nada.
   - **Archivo de Cemu (.wua)** o **Imagen de disco (.wud / .wux) con las claves**: un solo archivo, que
     puedes elegir desde cualquier carpeta. Una imagen de disco necesita además la clave del disco y la
     clave común (el `keys.txt` de Cemu sirve para las dos).

   Cuando eliges otro juego, la copia que la app hizo de la elección anterior se borra para liberar
   espacio; el juego que ya puedes jugar se conserva.
3. Pulsa **Start / resume / retry setup**. El dispositivo Android extrae y compila el juego una sola vez; la
   pantalla muestra el progreso, por ejemplo "Compiling: 30/80 · about 12 min left". Puedes salir
   de la app mientras tanto, y **Pause setup** lo detiene de forma segura.
4. Cuando indique que la instalación terminó, pulsa **Play current build**.

Los mods de código (por ejemplo las misiones de historia) necesitan el juego compilado con soporte
para mods: activa **Mods de código** en la app de instalación, antes o después de la primera instalación.
La compilación tarda más o menos lo mismo que la primera instalación; mientras tanto puedes seguir
jugando con la versión actual. Más detalles en [setup-code-mods.md](setup-code-mods.md#android) (en inglés).

### O bien: prepáralo en tu PC

1. Instala el SDK y el NDK de Android, JDK 17 o posterior, CMake, Ninja y Python 3.
2. Extrae tu juego y genera su código (pasos 1 y 2 de *Building* en el [README](../README.md)).
3. Ejecuta `android/build_native.sh` y después `./gradlew assembleRelease` dentro de `android/`.
4. Instala el APK por USB con `adb install -r app/build/outputs/apk/release/app-release.apk`.

Los pasos completos están en el README, sección *Android (build it yourself)*.

El salto lunar es un truco opcional en **Gameplay → Cheats**, desactivado de forma predeterminada. Actívalo y mantén **L** para ascender; la palanca izquierda dirige el vuelo con respecto a la cámara, incluso después de soltar L. Suelta la palanca para detener el movimiento horizontal. **No fall damage after moon jump** es una opción independiente que solo protege un vuelo iniciado con este truco. Ambas opciones funcionan a 30 fps, con interpolación y con true 60; cargar un estado borra el vuelo actual.

## Juego limpio

- Usa tu propia copia del juego, extraída de tu propio disco o consola.
- Un APK preparado en tu PC contiene tu juego recompilado. Úsalo solo en tu dispositivo Android y no lo
  compartas nunca.
- Si alguien ofrece un APK listo con el juego dentro, no es de este proyecto.

## Preguntas

**¿Necesito un mando?**
No. Los controles en pantalla cubren todo el GamePad. Un mando también funciona y oculta los
botones táctiles mientras está conectado.

**¿Qué versión del juego?**
La versión de Wii U de The Wind Waker HD, de EE. UU. o de Europa. Las partidas de una Wii U real
también funcionan.

**Mi dispositivo Android se calienta. ¿Es normal?**
El juego es exigente. Cuando el dispositivo Android se calienta, el juego baja solo de 60 a 30 fps estables
y sigue a velocidad completa.

**¿Qué ocurre al guardar un estado completo?**
Por defecto, Android pausa el juego mientras comprime la memoria en bloques pequeños y la escribe
en la ranura, sin crear otra imagen completa. El almacenamiento lento puede alargar la pausa.
El escritorio conserva la compresión rápida en segundo plano. Todas las versiones nuevas cargan
estados v3 de streaming y estados rápidos v1/v2 de cualquier plataforma; las versiones antiguas rechazan v3.

**Método para guardar estados completos:** En Save states, elige **Auto** (predeterminado), **Streaming** o **Fast**. Auto usa streaming con menos memoria en Android y la compresión rápida en segundo plano existente en Windows, macOS y Linux, también para Crash Recovery. Se guarda como `auto`, `streaming` o `fast` en `full_save_path.cfg`, junto a `full_save_states.cfg` en la carpeta de estados. Todas las versiones nuevas cargan estados v3 de streaming con cualquier opción; los estados antiguos compatibles siguen siendo legibles.

**Algo no funciona.**
Abre una [issue](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/issues) con el modelo del
dispositivo Android, la versión de Android y lo que pasó (en inglés si puedes). Después de un cierre
inesperado, la siguiente vez que abras el juego te ofrecerá el informe para compartirlo.
También cubre los cierres por falta de memoria en primer plano, señales y ANR registrados por Android,
si el sistema facilita el historial. El resumen incluye motivo, fecha, estado/señal, importancia del
proceso y la última muestra de memoria PSS/RSS. Omite descripciones, trazas, bytes del estado del
proceso y logcat para no incluir nombres, partidas ni rutas privadas. Los cierres normales, la
liberación de memoria en segundo plano y las paradas de las herramientas de partidas no generan un
informe del sistema. Si hay un informe nativo cercano en el tiempo, se incluye en el mismo archivo.
Compartir es opcional.

### Diagnóstico de memoria opcional

En ejecuciones de diagnóstico, `WWHD_VK_MEMORY_LOG_FRAMES=300` añade informes agregados
`[gfx] memory` cada 300 fotogramas del renderizador a los registros normales y de fallos.
Sin definir, con cero o con un valor inválido, desactiva la recopilación. Los informes
incluyen RSS actual, asignaciones de imágenes en caché, bloques de carga y capacidades
de datos de shaders retenidos; no contienen código de shaders ni datos de partidas.
RSS no es el total del heap ni una medición del máximo.

Tras compilar, el juego conserva solo el SPIR-V y un hash del texto GLSL de cada shader;
el texto se libera (define `WWHD_VK_SHADER_SOURCE_VERIFY=1` para conservarlo en diagnósticos).
Los contadores excluyen resultados del compilador, instantáneas de disco, metadatos,
sobrecarga del asignador e imágenes retiradas, fuera de caché o del controlador. Para
investigar la retención, usa resúmenes agregados del heap con una escena repetible, y
desactiva el diagnóstico al comparar rendimiento.


---

El port de Android es de [rhemfur](https://github.com/rhemfur) y forma parte de ZeldaWWHDRecomp.
Proyecto de fans, sin relación con Nintendo ni respaldo suyo. The Legend of Zelda y The Wind
Waker son marcas de Nintendo.

Para diagnósticos de fallos de desarrollo, `WWHD_CRASH_GUEST_MEMORY=1` añade una ventana limitada de 128 bytes por registro invitado solo si toda la ventana pertenece a una sección RPX ejecutable y no escribible. Se conserva una referencia ejecutable durante la carga, después de las copias del cargador. Al fallar, lecturas volátiles limitadas comparan los 128 bytes invitados actuales con esa referencia; solo una coincidencia completa muestra bytes de referencia. El código modificado o no disponible se oculta por completo, sin bytes modificados ni hashes. La muestra no es atómica respecto a escrituras invitadas; se ocultan heap, pila, datos escribibles, rangos desconocidos y límites de sección. No se añaden ASCII, nombres de hilos ni contenido de partidas. El informe incluye una instantánea numérica reciente de las regiones de direcciones invitadas, número de hilos registrados y RSS del proceso (puede no estar disponible). La opción está desactivada por defecto y así no consulta memoria ni copia imágenes. El código original conservado está limitado a 16 MiB y 64 secciones sin solapamientos; los rangos mayores o ambiguos se ocultan. Los bytes de código originales pueden identificar la versión del juego; revise el informe antes de compartirlo.
