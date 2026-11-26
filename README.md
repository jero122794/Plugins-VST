# Plugins-VST

Plugins de audio propios para uso personal. Formatos: **AU** (Logic), **VST3** y **CLAP** (Reaper), y una versión standalone.

| Plugin | Qué es |
|---|---|
| **MasterChain** | Cadena de mastering completa: entrada, EQ, compresor, saturación, imagen, clipper, limitador true-peak y dither, con medición EBU R128 |
| **MC Limiter** · **MC BusComp** · **MC Saturator** · **MC Imager** · **MC Clipper** · **MC Meter** | Cada módulo de la cadena como plugin suelto (mismo código) |
| **PrismaEQ** | EQ dinámico de 8 bandas con analizador (también es el EQ de MasterChain) |
| **OptoComp** | Compresor óptico: T-2 (válvula) / T-3 (estado sólido) |
| **Furia** | Compresor de carácter: ratios 1:1 a Nuke y Opto, detector con HP / realce / link, distorsión de 2º y 3º armónico |
| **Afina** | Corrección de afinación en tiempo real por escala (detección YIN + resíntesis PSOLA con conservación de formantes) |

## Compilar en macOS

Requisitos: Xcode Command Line Tools (`xcode-select --install`), CMake ≥ 3.22 y Ninja (`brew install cmake ninja`).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

La primera vez se descargan JUCE y las extensiones CLAP. La compilación completa de los 9 plugins tarda (LTO activado): cuenta con 20–40 min en un Mac con Apple Silicon. Para compilar solo uno: `cmake --build build --target MasterChain_AU` (o `_VST3`, `_CLAP`).

Al terminar, los plugins se copian solos a `~/Library/Audio/Plug-Ins/{Components,VST3,CLAP}`. Binario universal (Apple Silicon + Intel).

- **Logic**: si no aparecen, ejecuta `killall -9 AudioComponentRegistrar` y vuelve a abrir Logic.
- **Reaper**: *Preferences → Plug-ins → VST/CLAP → Re-scan*.

También puedes descargar los binarios ya compilados en la pestaña **Actions** del repositorio (artefacto `Plugins-macOS`), validados con `auval` y `pluginval`.

## MasterChain: cómo está pensado

```
Input → [ EQ · Compresor · Saturación · Imagen · Clipper ] → Limitador → Dither
         └──────────── orden libre (flechas) ─────────────┘
```

- **Orden protegido.** La entrada va siempre primero; el limitador y el dither, siempre al final. Así nunca se satura después del limitador (rompería el techo de true peak) ni se aplica dither antes de procesar.
- **Cada módulo se activa por separado.** Por defecto están activos la entrada, el EQ (plano) y el limitador: si solo quieres limitar, ya está.
- **El bypass no desalinea nada.** Un módulo en bypass retrasa la señal lo mismo que si procesara, y el cambio se hace con un crossfade de 20 ms. Un test comprueba que, con todo en bypass, la salida es exactamente la entrada retrasada la latencia que se reporta al DAW.
- **A/B igualado.** Escucha el original a la misma sonoridad que el procesado. Si el procesado solo te parece mejor porque suena más fuerte, aquí lo descubres.
- **Plataforma.** Indica cuánto bajará tu máster Spotify, Apple Music, YouTube, Tidal, Amazon o Deezer. "Escuchar como la plataforma" aplica esa bajada: **desactívalo antes de exportar** (la interfaz muestra un aviso rojo mientras está activo).

### Medición (EBU R128 / ITU-R BS.1770-4)

Momentary, short-term, integrado, LRA, true peak, PLR, correlación, histograma de loudness e historial de 60 s. Validado contra los casos de prueba oficiales **EBU Tech 3341** (loudness y true peak) y **EBU Tech 3342** (LRA): integrado dentro de ±0,02 LU y true peak exacto (−6,02 dBTP en los casos de −6 dBTP).

### Módulos

| Módulo | Controles | Notas |
|---|---|---|
| Input | Ganancia, subsónico (DC/20/30 Hz, 24 dB/oct), invertir L/R, escucha en mono | |
| EQ | El PrismaEQ completo | Bandas dinámicas: cubren lo que en mastering se suele hacer con multibanda |
| Compresor | Threshold, ratio, attack, release, release automático, knee, HPF del sidechain, makeup, mix | VCA de bus; el HPF del sidechain (90 Hz por defecto) evita que el bombo haga bombear la mezcla |
| Saturación | Cinta / válvula / transformador, drive, mix, output, compensación de nivel | 4x sobremuestreo |
| Imagen | Ancho M/S, graves en mono por debajo de X Hz | Linkwitz-Riley 24 dB/oct sobre el canal Side |
| Clipper | Drive, techo, suavidad | 4x; recorta transitorios para que el limitador trabaje menos |
| Limitador | Ganancia, techo (dBTP), release, lookahead, true peak | Techo garantizado por construcción; test con toda la cadena saturada: −0,998 dBTP con techo −1 |
| Dither | 16/20/24 bits, noise shaping | Actívalo solo al exportar a menos de 32 bits flotantes |

## PrismaEQ: uso

- **Doble clic** en el gráfico: crea una banda (corte en los extremos, campana en el resto). Doble clic en un nodo: la apaga.
- **Arrastrar** un nodo: frecuencia y ganancia. **Rueda del ratón** sobre un nodo: Q.
- **Dyn Range** distinto de 0 convierte la banda en dinámica: por encima de **Threshold** la ganancia se desplaza hasta ese valor (negativo = comprime esa zona, positivo = la expande). La línea vertical del nodo muestra cuánto se está moviendo; la curva discontinua es la estática.
- **HQ**: procesa a 2x. A 44,1/48 kHz, las campanas por encima de ~10 kHz se estrechan cerca de Nyquist; HQ lo corrige a cambio de 3 muestras de latencia.
- La curva dibujada es exactamente la que se aplica al audio (test: error de 0,000 dB).
- **Fase mínima / Fase lineal**. En fase lineal el EQ no desplaza la fase (ideal en mastering): un FIR de 16384 taps a 44,1/48 kHz (32768 a 88,2/96) diseñado en segundo plano a partir de la curva casi analógica, así que tampoco hay cramping. Error de magnitud medido: 0,006 dB entre 60 Hz y 20 kHz. Coste: ~176 ms de latencia (Logic y Reaper la compensan) y las bandas dinámicas actúan como estáticas. Mid y Side se respetan (se procesa en M/S con dos FIR); Left/Right se tratan como Stereo.

## Afina: corrección de afinación

| Control | Qué hace |
|---|---|
| Tonalidad / Escala | Notas permitidas (cromática, mayor, menores, dórica, pentatónicas, blues) |
| Teclado | Clic en una nota de la escala para excluirla a mano |
| Velocidad | 0 ms = corrección instantánea (efecto robótico); 20–60 ms = natural; >100 ms = muy sutil |
| Cantidad | Qué parte de la desafinación se corrige (100 % = toda) |
| Transponer | ±12 semitonos, conservando los formantes |
| Rango de voz | Nota más grave a detectar: Soprano (~24 ms de latencia), Alto/Tenor (~46 ms), Voz grave (~55 ms) |

Medido en los tests: detección con error < 0,5 cents; una nota cantada +39 cents se corrige a 0,1 cents; sin corrección la salida es la entrada retrasada (correlación 1,0000); al subir 3 semitonos el formante se mueve un 7 % (un remuestreo lo movería un 19 %); 0 errores de octava en 9831 estimaciones de una melodía con ataques.

Notas: en estéreo se corrige la suma mono (la voz principal es mono). Los primeros ~25 ms de cada nota pasan sin corregir, para que el ataque no produzca saltos de octava.

## Furia: controles

| Control | Qué hace |
|---|---|
| Input | Empuja la señal contra un umbral fijo (-18 dB): más input = más compresión |
| Attack / Release | 1–10: de 0,05 a 30 ms y de 50 ms a 3,5 s |
| Output | ±20 dB |
| Ratio | 1:1 (solo color: con Dist 2/3 satura sin comprimir), 2, 3, 4, 6, 10, 20, Nuke (casi un muro) y Opto (10:1 con release que se alarga con la compresión sostenida) |
| Detector | HP (el bombo deja de hacer bombear la mezcla), Band (+6 dB a 6 kHz en el detector: de-esser), Link (estéreo enlazado) |
| Audio | HP 80 Hz en la ruta de audio, Dist 2 (2º armónico, cálido) o Dist 3 (3º, tipo cinta); sobremuestreado |
| Mix | Compresión en paralelo (Dry ↔ Comp) |
| Power | Bypass con la latencia compensada (exacto, verificado en tests) |

Los botones RATIO, DETECTOR y AUDIO recorren los estados como en el hardware; también puedes hacer clic directamente en cada LED.

## OptoComp: controles

| Control | Qué hace |
|---|---|
| Model | LA-2A: ataque ~10 ms, release de 1 a 15 s según el programa, etapa de válvula (2º armónico). LA-3A: ataque ~2,5 ms, release más corto, etapa de transistor (3er armónico, más limpia) |
| Peak Reduction | Cantidad de compresión |
| Gain | Make-up, antes de la etapa de salida: subirlo satura más |
| Compress / Limit | ≈ 3,5:1 de rodilla blanda / ≈ 12:1 de rodilla más dura |
| HF Emphasis | Realza los agudos en el sidechain: comprime más las sibilantes y menos los graves |
| Color | Saturación de la etapa de salida y del transformador (0 = transparente) |
| Mix | Compresión en paralelo, con compensación de latencia |
| Oversampling | 1x / 2x / 4x para la etapa no lineal |
| Stereo Link | Una sola célula para los dos canales o una por canal |

Las constantes de tiempo están calibradas con las especificaciones publicadas de los equipos, no con mediciones de una unidad real.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

| Test | Qué comprueba |
|---|---|
| `loudness_tests` | Casos EBU Tech 3341 y 3342 |
| `mastering_tests` | Techo del limitador, dither, compresor de bus, saturación, imagen, entrada |
| `eq_tests` | Curva dibujada = audio medido, pendientes, bandas dinámicas, M/S |
| `dsp_tests` | Modelo óptico de OptoComp |
| `furia_tests` | Curvas de ratio, tiempos, modo Opto, detector, distorsión de Furia |
| `pitch_tests` | Detección, corrección, escalas, velocidad, transparencia, formantes y ataques de Afina |
| `*_ProcessorTests` | Cada plugin como lo usa un DAW: latencia, bypass, estado, bloques irregulares, A/B igualado |

## Estructura

```
libs/dspcore/     DSP en C++ puro, sin JUCE (todo lo que se puede testear con números)
libs/studioui/    Estilo visual y EQ compartido (EqCore + EqView)
libs/mastering/   Framework de módulos de mastering, paneles y medidor
plugins/          Un directorio por plugin (MasterModules genera los 6 sueltos)
tests/            Tests del DSP
cmake/            Dependencias (JUCE 8, clap-juce-extensions) y función add_studio_plugin
```
