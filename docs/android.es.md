# Wind Waker HD en Android

[English](android.md) · [Português](android.pt.md) · **Español**

The Wind Waker HD funciona de forma nativa en teléfonos y tabletas Android. No es un emulador: el
propio código del juego se recompila para procesadores ARM y dibuja con Vulkan. Necesitas tu propia
copia del juego de Wii U; este proyecto nunca proporciona archivos del juego.

## Qué hace la versión de Android

| | |
|---|---|
| **Controles en pantalla** | Los dos sticks, la cruceta, A B X Y, L R ZL ZR y + −. El botón 🎮 debajo del botón de vista (esquina superior izquierda) los muestra u oculta. Se ocultan mientras hay un mando conectado y vuelven con el siguiente toque. Un mando Bluetooth o USB también funciona. |
| **La pantalla del GamePad al pausar** | Pulsa + y la imagen cambia a la pantalla del GamePad (mapa y objetos), como el menú de pausa de la versión de GameCube. Pulsa + otra vez para volver. |
| **60 fps sin que el juego vaya lento** | En los teléfonos rápidos el juego muestra 60 imágenes por segundo. Cuando el teléfono se calienta, pasa solo a 30 estables, y el juego nunca se ralentiza. |
| **Teléfonos antiguos** | Si el driver de tu GPU Adreno es demasiado antiguo, la pantalla de error ofrece instalar un driver como Mesa Turnip. |
| **Partidas guardadas** | Exporta e importa tus partidas y guárdalas en la copia de seguridad de Android. Las partidas de una Wii U real (de EE. UU. o de Europa) también funcionan. |

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

Necesitas un teléfono de 64 bits con Android 13 o posterior y Vulkan 1.3 (o un driver
personalizado en Adreno), y unos pocos GB libres.

## Cómo jugar

### La app de instalación (sin PC)

La app de instalación ya está en el proyecto ([#98](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/pull/98))
y todavía se está probando antes de una versión oficial. No lleva código del juego: prepara el juego
en tu teléfono a partir de tu propia copia. Hasta que salga en una versión, quien quiera probarla
puede descargar el APK de prueba (arm64) de las [compilaciones de Android](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/actions/workflows/android-toolchain.yml): abre
la ejecución correcta más reciente y descarga el artefacto `android-hosted-clang-arm64-v8a` (hace
falta iniciar sesión en GitHub). El APK está en
`build/tester-apk/wwhd-ondevice-debug-arm64-v8a.apk` dentro del zip.

1. Instala el APK. Si ya tienes una versión preparada en tu PC, exporta antes tu partida (mantén
   pulsado el icono de la app) y desinstala esa versión: las dos están firmadas de forma distinta.
2. Abre la app y elige tu juego: **Choose extracted game folder** (carpeta extraída), **Choose WUA
   archive** (archivo `.wua`) o **Choose WUD / WUX disc image** (imagen de disco; después también
   **Choose disc key file** y **Choose common key file**, las claves).
3. Pulsa **Start / resume / retry setup**. El teléfono extrae y compila el juego una sola vez; la
   pantalla muestra el progreso, por ejemplo "Compiling: 30/80 · about 12 min left". Puedes salir
   de la app mientras tanto, y **Pause setup** lo detiene de forma segura.
4. Cuando indique que la instalación terminó, pulsa **Play current build**.

### O bien: prepáralo en tu PC

1. Instala el SDK y el NDK de Android, JDK 17 o posterior, CMake, Ninja y Python 3.
2. Extrae tu juego y genera su código (pasos 1 y 2 de *Building* en el [README](../README.md)).
3. Ejecuta `android/build_native.sh` y después `./gradlew assembleRelease` dentro de `android/`.
4. Instala el APK por USB con `adb install -r app/build/outputs/apk/release/app-release.apk`.

Los pasos completos están en el README, sección *Android (build it yourself)*.

## Juego limpio

- Usa tu propia copia del juego, extraída de tu propio disco o consola.
- Un APK preparado en tu PC contiene tu juego recompilado. Úsalo solo en tu teléfono y no lo
  compartas nunca.
- Si alguien ofrece un APK listo con el juego dentro, no es de este proyecto.

## Preguntas

**¿Necesito un mando?**
No. Los controles en pantalla cubren todo el GamePad. Un mando también funciona y oculta los
botones táctiles mientras está conectado.

**¿Qué versión del juego?**
La versión de Wii U de The Wind Waker HD, de EE. UU. o de Europa. Las partidas de una Wii U real
también funcionan.

**¿Por qué todavía no hay un APK en las versiones oficiales?**
Un APK preparado en un PC contiene el juego recompilado, que no se puede compartir. La app de
instalación prepara el juego en tu teléfono, así que la propia app sí se puede compartir. Ya está
en el proyecto y entrará en una versión cuando terminen las pruebas que faltan en teléfonos.

**Mi teléfono se calienta. ¿Es normal?**
El juego es exigente. Cuando el teléfono se calienta, el juego baja solo de 60 a 30 fps estables
y sigue a velocidad completa.

**Algo no funciona.**
Abre una [issue](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/issues) con el modelo del
teléfono, la versión de Android y lo que pasó (en inglés si puedes). Después de un cierre
inesperado, la siguiente vez que abras el juego te ofrecerá el informe para compartirlo.

---

El port de Android es de [rhemfur](https://github.com/rhemfur) y forma parte de ZeldaWWHDRecomp.
Proyecto de fans, sin relación con Nintendo ni respaldo suyo. The Legend of Zelda y The Wind
Waker son marcas de Nintendo.
