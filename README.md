# Plugins-VST

Plugins de audio propios para uso personal. Formatos: **AU** (Logic), **VST3** y **CLAP** (Reaper), y una versión standalone.

| Plugin | Estado | Descripción |
|---|---|---|
| **OptoComp** | v0.1 – funcional | Compresor óptico con dos modelos: LA-2A (válvula) y LA-3A (estado sólido) |
| **PrismaEQ** | v0.1 – funcional | EQ de 8 bandas dinámico, Stereo/Mid/Side/L/R, analizador pre/post, modo HQ 2x |
| Corrección de tono | pendiente | Detección YIN + resíntesis PSOLA con preservación de formantes |

## Compilar en macOS

Requisitos: Xcode Command Line Tools (`xcode-select --install`), CMake ≥ 3.22 y Ninja (`brew install cmake ninja`).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

La primera vez se descargan JUCE y las extensiones CLAP (~5 min). Al terminar, los plugins se copian solos a `~/Library/Audio/Plug-Ins/{Components,VST3,CLAP}`. El binario es universal (Apple Silicon + Intel).

- **Logic**: si no aparece, reinicia Logic o ejecuta `killall -9 AudioComponentRegistrar` y vuelve a abrirlo.
- **Reaper**: *Preferences → Plug-ins → VST/CLAP → Re-scan*.

También puedes descargar los binarios ya compilados en la pestaña **Actions** del repositorio (artefacto `Plugins-macOS`).

## Tests

```bash
ctest --test-dir build --output-on-failure
```

- `dsp_tests`: mide el comportamiento del modelo (ratio, ataque, release en dos fases, release dependiente del programa, armónicos de válvula frente a transistor, estabilidad).
- `OptoComp_ProcessorTests`: instancia el procesador como lo haría un DAW (mono/estéreo, bloques irregulares, cambio de oversampling en caliente, latencia, estado).

El CI de macOS además valida el AU con `auval` y el VST3 con `pluginval` en su nivel de exigencia máximo (10).

## Estructura

```
libs/dspcore/     DSP en C++ puro, sin JUCE, reutilizable entre plugins
plugins/OptoComp/ Wrapper JUCE (procesador + UI) y tests de integración
tests/            Tests del DSP
cmake/            Dependencias (JUCE 8, clap-juce-extensions)
```

## PrismaEQ: uso

- **Doble clic** en el gráfico: crea una banda (corte en los extremos, campana en el resto). Doble clic en un nodo: la apaga.
- **Arrastrar** un nodo: frecuencia y ganancia. **Rueda del ratón** sobre un nodo: Q.
- **Dyn Range** distinto de 0 convierte la banda en dinámica: por encima de **Threshold** la ganancia se desplaza hasta ese valor (negativo = la banda comprime, positivo = expande). La línea vertical del nodo muestra cuánto se está moviendo, y la curva discontinua es la estática.
- **HQ**: procesa a 2x. A 44,1/48 kHz, las campanas por encima de ~10 kHz se estrechan cerca de Nyquist (cramping); HQ lo corrige a cambio de 3 muestras de latencia.
- Lo que dibuja la curva es exactamente lo que se aplica al audio: hay un test que lo verifica (error de 0,000 dB).
- Limitación de la v0.1: no hay modo de fase lineal.

## OptoComp: controles

| Control | Qué hace |
|---|---|
| Model | LA-2A: ataque ~10 ms, release de 1 a 15 s según el programa, etapa de válvula (2º armónico). LA-3A: ataque ~2,5 ms, release más corto, etapa de transistor (3er armónico, más limpia) |
| Peak Reduction | Cantidad de compresión (sidechain hacia la célula óptica) |
| Gain | Make-up, antes de la etapa de salida: subirlo satura más |
| Compress / Limit | ≈ 3,5:1 de rodilla blanda / ≈ 12:1 de rodilla más dura |
| HF Emphasis | Realza los agudos en el sidechain: comprime más las sibilantes y menos los graves |
| Color | Cantidad de saturación de la etapa de salida y del transformador (0 = transparente). No existe en el hardware |
| Mix | Compresión en paralelo, con compensación de latencia |
| Oversampling | 1x / 2x / 4x para la etapa no lineal (2x por defecto, latencia de 3 muestras) |
| Stereo Link | Una sola célula para los dos canales (por defecto) o una por canal |

### Limitaciones conocidas

- Las constantes de tiempo están calibradas con las especificaciones publicadas de los equipos, no con mediciones de un equipo real. El modelo LA-3A es el más aproximado. Para afinarlo de verdad hacen falta capturas (escalones, ráfagas y barridos) de una unidad física.
- La etapa de válvula es un modelo estático (waveshaper con bias), no una simulación de circuito.
