# Guía de instalación

Los plugins se distribuyen en tres formatos:

| Formato | macOS | Windows | DAW típicos |
|---|---|---|---|
| **AU** (`.component`) | ✔ | — | Logic Pro, GarageBand (solo existe en Mac) |
| **VST3** (`.vst3`) | ✔ | ✔ | Reaper, Cubase, Ableton Live, Studio One, FL Studio… |
| **AAX** (`.aaxplugin`) | ✔ | ✔ | Pro Tools (ver [la sección de Pro Tools](#pro-tools-aax): requiere firma) |
| **CLAP** (`.clap`) | ✔ | ✔ | Reaper, Bitwig |

Hay dos caminos: **descargar los binarios ya compilados** (rápido) o **compilar desde el código** (si quieres modificar algo).

Plugins incluidos: MasterChain, MC Limiter, MC BusComp, MC Saturator, MC Imager, MC Clipper, MC Meter, PrismaEQ, OptoComp, Furia y Afina.

---

## macOS

Requisitos: macOS 11 (Big Sur) o posterior. Binario universal: funciona en Apple Silicon (M1–M4) e Intel.

### Opción A: descargar los binarios

1. En GitHub, abre el repositorio → pestaña **Actions** → la ejecución más reciente con ✓ verde en `main`.
2. Al final de la página, en **Artifacts**, descarga **`Plugins-macOS`**. Contiene `Plugins-macOS.zip` con las carpetas `AU`, `VST3`, `AAX` y `CLAP`.
3. Descomprime y copia cada tipo en su carpeta (en Finder: **Ir → Ir a la carpeta…** y pega la ruta):

   | Copiar el contenido de | A |
   |---|---|
   | `AU` (`.component`) | `~/Library/Audio/Plug-Ins/Components` |
   | `VST3` (`.vst3`) | `~/Library/Audio/Plug-Ins/VST3` |
   | `CLAP` (`.clap`) | `~/Library/Audio/Plug-Ins/CLAP` (créala si no existe) |

4. **Quita la cuarentena.** Los plugins no están firmados con un certificado de Apple: macOS bloquea los archivos descargados de Internet que no lo estén. Abre **Terminal** y ejecuta:

   ```bash
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/CLAP
   ```

5. Sigue con [Activarlos en el DAW](#activarlos-en-el-daw).

### Opción B: compilar desde el código

1. Instala las herramientas de desarrollo de Apple:

   ```bash
   xcode-select --install
   ```

2. Instala [Homebrew](https://brew.sh) si no lo tienes, y después CMake y Ninja:

   ```bash
   brew install cmake ninja
   ```

3. Descarga el código y compila:

   ```bash
   git clone https://github.com/jero122794/Plugins-VST.git
   ```

   ```bash
   cd Plugins-VST
   ```

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   ```

   ```bash
   cmake --build build
   ```

   La primera compilación descarga JUCE y tarda entre 20 y 40 minutos. **Al terminar, los plugins se copian solos** a `~/Library/Audio/Plug-Ins/` (no hace falta quitar la cuarentena: no vienen de Internet).

   Para compilar solo uno: `cmake --build build --target MasterChain_AU` (o `_VST3`, `_CLAP`, `_Standalone`).

---

## Windows

Requisitos: Windows 10 u 11 de 64 bits.

### Opción A: descargar los binarios

1. En GitHub: **Actions** → ejecución más reciente con ✓ verde → **Artifacts** → descarga **`Plugins-Windows`**. Contiene `Plugins-Windows.zip` con las carpetas `VST3`, `AAX` y `CLAP`.
2. Copia (necesitarás permiso de administrador):

   | Copiar el contenido de | A |
   |---|---|
   | `VST3` (carpetas `.vst3`, cópialas enteras) | `C:\Program Files\Common Files\VST3` |
   | `CLAP` (archivos `.clap`) | `C:\Program Files\Common Files\CLAP` (créala si no existe) |

3. Sigue con [Activarlos en el DAW](#activarlos-en-el-daw).

### Opción B: compilar desde el código

1. Instala **Visual Studio Community** (2022 o posterior) (gratuito) con la carga de trabajo **"Desarrollo para el escritorio con C++"**. Incluye CMake.
2. Instala [Git para Windows](https://git-scm.com/download/win).
3. Abre **"x64 Native Tools Command Prompt for VS"** (búscalo en el menú Inicio) y ejecuta:

   ```bash
   git clone https://github.com/jero122794/Plugins-VST.git
   ```

   ```bash
   cd Plugins-VST
   ```

   ```bash
   cmake -S . -B build -A x64
   ```

   ```bash
   cmake --build build --config Release --parallel
   ```

4. Los plugins quedan en `build\plugins\<Plugin>\<Plugin>_artefacts\Release\VST3` y `...\CLAP`. Cópialos a las carpetas de la tabla de la opción A. En Windows la copia no es automática porque escribir en `Program Files` requiere permisos de administrador.

---

## Activarlos en el DAW

### Logic Pro (Mac, formato AU)

1. Cierra Logic. Para que vuelva a buscar plugins nuevos, en Terminal:

   ```bash
   killall -9 AudioComponentRegistrar
   ```

2. Abre Logic. Validará los plugins nuevos (puede tardar unos segundos).
3. Aparecen en **Audio Units → Plugins-VST** al insertar un efecto en un canal.
4. Si alguno sale como "no validado": **Logic Pro → Ajustes → Plug-in Manager**, selecciónalo y pulsa **Restablecer y volver a escanear la selección**.

### Reaper (Mac y Windows, VST3 y CLAP)

1. **Options → Preferences → Plug-ins → VST** (o **CLAP**).
2. Comprueba que la carpeta de instalación está en la lista de rutas y pulsa **Re-scan**.
3. Busca los plugins por nombre (por ejemplo "MasterChain") en el navegador de FX.

> Si tienes las versiones VST3 y CLAP a la vez, Reaper muestra las dos. Son idénticas: usa la que prefieras.

### Pro Tools (Mac y Windows, formato AAX)

> **Importante:** Pro Tools normal (Artist, Studio, Ultimate) **solo carga plugins AAX firmados digitalmente por PACE**. Los `.aaxplugin` que genera este proyecto no están firmados: tal cual solo los carga **Pro Tools Developer**, una versión gratuita para desarrolladores.

**1. Instalar los `.aaxplugin`** (carpeta `AAX` del zip, o `build/AAX` si compilas en Mac). Hace falta permiso de administrador:

| Sistema | Carpeta |
|---|---|
| macOS | `/Library/Application Support/Avid/Audio/Plug-Ins` |
| Windows | `C:\Program Files\Common Files\Avid\Audio\Plug-Ins` |

En Mac, por Terminal (desde la carpeta donde están los `.aaxplugin`):

```bash
sudo cp -R *.aaxplugin "/Library/Application Support/Avid/Audio/Plug-Ins/"
```

```bash
sudo xattr -dr com.apple.quarantine "/Library/Application Support/Avid/Audio/Plug-Ins"
```

**2. Elegir cómo cargarlos:**

- **Sin firmar → Pro Tools Developer.** Crea una cuenta gratuita en el programa de desarrolladores de Avid (developer.avid.com) y descarga *Pro Tools Developer*. Carga plugins sin firmar. Sirve para probar y para trabajar.
- **Firmados → cualquier Pro Tools.** Hay que firmarlos con la herramienta `wraptool` de PACE:
  1. Solicita acceso a las herramientas de firma de PACE (*PACE Eden / Fusion*) escribiendo a PACE como desarrollador de AAX. Avid lo gestiona desde su programa de desarrolladores.
  2. Necesitas una cuenta de iLok y un certificado de firma. En Mac, además, un certificado de *Developer ID* de Apple (programa de desarrolladores de Apple, de pago).
  3. Firma cada plugin:

     ```bash
     wraptool sign --verbose --account TU_CUENTA_PACE --wcguid TU_WCGUID --signid "Developer ID Application: TU NOMBRE" --in MasterChain.aaxplugin --out MasterChain.aaxplugin
     ```

**3.** Abre Pro Tools: los plugins aparecen en el menú de inserciones, en la categoría correspondiente (Dynamics, EQ, Pitch Shift, Harmonic, Sound Field).

### Otros DAW con VST3

Cubase, Ableton Live, Studio One, FL Studio y Bitwig buscan automáticamente en la carpeta VST3 estándar. Si no aparecen, busca la opción "volver a escanear plugins" en sus preferencias.

---

## Resolución de problemas

| Problema | Solución |
|---|---|
| macOS dice que el plugin "está dañado" o "no se puede abrir" | Es la cuarentena de los archivos descargados: ejecuta el comando `xattr` del paso 4 de la opción A |
| Logic no muestra el plugin | `killall -9 AudioComponentRegistrar` y reabrir Logic; después revisa el Plug-in Manager |
| Reaper no lo encuentra | Comprueba la ruta en Preferences → Plug-ins y pulsa Re-scan (en Windows, que la carpeta `.vst3` se copió entera) |
| La pista suena desplazada en el tiempo | Activa la compensación de latencia del DAW (activada por defecto en Logic y Reaper). Todos los plugins reportan su latencia exacta |
| CPU alta con PrismaEQ / MasterChain | La fase lineal y el HQ del EQ son lo más costoso: úsalos al mezclar o masterizar, no mientras grabas |
| Pro Tools no muestra los plugins AAX | Pro Tools normal solo carga plugins firmados por PACE: usa Pro Tools Developer o fírmalos (ver la sección de Pro Tools) |
| Afina tiene demasiada latencia para monitorizar en directo | Pon **Rango de voz** en *Soprano* (~24 ms) |

## Desinstalar

Borra los archivos de las carpetas de la tabla de instalación y vuelve a escanear en el DAW.
