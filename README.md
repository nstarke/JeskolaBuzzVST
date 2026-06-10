![images/logo.gif](images/logo.gif)

# BuzzBridge VST3

[![CI](https://github.com/nstarke/JeskolaBuzzVST/actions/workflows/ci.yml/badge.svg)](https://github.com/nstarke/JeskolaBuzzVST/actions/workflows/ci.yml)

![images/image.png](images/image.png)

A VST3 wrapper that loads [Jeskola Buzz](https://jeskola.net/buzz/) machine DLLs and runs them inside any VST3-compatible DAW. Buzz machines are 32-bit plugin DLLs that represent virtual instruments and effects in the Buzz music tracker.

BuzzBridge exposes two VST3 plugins in a single bundle:

- **Buzz Generator Bridge** -- loads Buzz generator (instrument) DLLs
- **Buzz Effect Bridge** -- loads Buzz effect DLLs

Both 32-bit and 64-bit DAWs are supported. All Buzz machine parameters are mapped to automatable VST3 sliders with correct names, ranges, and defaults. MIDI note and CC input is fully supported.

## Requirements

- **Windows** (Buzz machines are Win32 DLLs)
- **Visual Studio 2022** with C++ desktop workload
- **CMake 3.25+**
- **Git** (for submodule checkout)
- **Inno Setup 6** (optional, for building the installer -- `choco install innosetup`)

## Build

```bash
git clone --recursive https://github.com/nstarke/JeskolaBuzzVST.git
cd JeskolaBuzzVST
```

If you already cloned without `--recursive`, initialize the VST3 SDK submodule:

```bash
git submodule update --init --recursive
```

### Quick build (both architectures)

```bat
scripts\build.bat
```

This builds everything in one step: 32-bit plugin, 32-bit bridge host, runs tests, builds the 64-bit plugin, and assembles the combined bundle in `dist/`.

### Manual build

```bash
# 32-bit build (direct-mode plugin + bridge host + tests)
cmake -B build32 -G "Visual Studio 17 2022" -A Win32
cmake --build build32 --config Release

# 64-bit build (bridge-mode plugin)
cmake -B build64 -G "Visual Studio 17 2022" -A x64
cmake --build build64 --config Release --target BuzzBridge
```

The 32-bit build must be done first because it produces `BuzzBridgeHost32.exe`, which the 64-bit plugin requires at runtime.

## Build Output

After a successful build, the combined VST3 bundle is at:

```
dist/BuzzBridge.vst3/
    Contents/
        x86-win/
            BuzzBridge.vst3          <-- 32-bit plugin (loads machines directly)
        x86_64-win/
            BuzzBridge.vst3          <-- 64-bit plugin (uses bridge)
            BuzzBridgeHost32.exe     <-- 32-bit bridge host (used by 64-bit plugin)
        Resources/
            moduleinfo.json
```

If building manually, the individual outputs are at:

```
build32/VST3/Release/BuzzBridge.vst3/Contents/x86-win/BuzzBridge.vst3
build32/bin/Release/BuzzBridgeHost32.exe
build64/VST3/Release/BuzzBridge.vst3/Contents/x86_64-win/BuzzBridge.vst3
```

## Building the Installer

The installer is built with [Inno Setup](https://jrsoftware.org/issetup.php). Install it first:

```bat
choco install innosetup --yes
```

Then, after a successful `scripts\build.bat` (which assembles the `dist/` folder), build the installer:

```bat
:: From Command Prompt (cmd):
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DMyAppVersion=v1.0.0 installer\BuzzBridge.iss

:: From PowerShell:
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" "/DMyAppVersion=v1.0.0" installer\BuzzBridge.iss
```

This produces `dist\BuzzBridge-Setup-v1.0.0.exe`. The `scripts\build.bat` script will also build the installer automatically if Inno Setup is detected.

The `/DMyAppVersion=` flag sets the version shown in the installer and the output filename. Use any string you like for local builds.

## Installation

### From installer (recommended)

Download `BuzzBridge-Setup-vX.Y.Z.exe` from the [Releases](https://github.com/nstarke/JeskolaBuzzVST/releases) page and run it. The installer places the VST3 bundle in `C:\Program Files\Common Files\VST3\BuzzBridge.vst3\` and provides an uninstaller via Windows Add/Remove Programs.

### Manual installation

Copy the entire `BuzzBridge.vst3` folder (from `dist/` or assembled manually) to your VST3 plugin directory:

```
C:\Program Files\Common Files\VST3\
```

If you are using a **64-bit DAW** (most modern DAWs), make sure `BuzzBridgeHost32.exe` is in the same folder as the 64-bit `BuzzBridge.vst3` DLL (`Contents/x86_64-win/`). The 64-bit plugin looks for it there at runtime.

If you only need the **32-bit plugin** (for a 32-bit DAW), you only need the `x86-win/` folder and can skip the 64-bit build entirely.

## Linux (WINE + yabridge)

BuzzBridge can be cross-compiled from Linux and run in Linux DAWs (Bitwig, Reaper-native, Ardour, etc.) via [yabridge](https://github.com/robbert-vdh/yabridge), which transparently bridges Windows VST3 plugins to native Linux VST3 hosts over WINE.

### How it works

Buzz machine DLLs are 32-bit Win32 code, so any Linux host needs a Windows execution environment to load them. The cross-compiled BuzzBridge is itself a 32-bit Windows VST3 plugin. yabridge wraps it as a native Linux VST3 that your DAW can load, and the 32-bit Buzz DLLs run inside the yabridge WINE host process. If a buggy Buzz machine crashes, yabridge isolates the failure from the Linux DAW.

You do not need the 64-bit bridge host (`BuzzBridgeHost32.exe`) on Linux — yabridge already handles the 64↔32 boundary at the VST3 layer, so only the 32-bit plugin is built.

### Requirements

- Linux (or WSL2) with `bash`, `cmake` ≥ 3.25, `ninja`, `git`, `curl`, `tar`
- [WINE](https://www.winehq.org/) **with 32-bit support** — the bridge is a 32-bit plugin and needs a 32-bit WINE loader and a win32 prefix (the installer sets this up; see [WINE 32-bit prefix and runtime](#wine-32-bit-prefix-and-runtime-environment))
- [yabridge](https://github.com/robbert-vdh/yabridge) + `yabridgectl` (for installing into DAWs)
- A Linux-native VST3 host (Bitwig Studio, Reaper, Ardour, Carla, etc.)

`llvm-mingw` (the clang-based mingw-w64 toolchain used for cross-compilation) is downloaded automatically into `third_party/llvm-mingw/` on first run.

### Building

From the repo root:

```bash
./scripts/build-linux-wine.sh
```

On first run this will:

1. Download llvm-mingw (≈400 MB) into `third_party/llvm-mingw/`.
2. Initialize the VST3 SDK submodule if needed.
3. Apply an idempotent patch to `sdk/public.sdk/source/vst/utility/alignedalloc.h` so it uses mingw's `_aligned_malloc` rather than the unavailable `std::aligned_alloc`.
4. Configure and build the 32-bit `BuzzBridge` target with clang targeting `i686-w64-mingw32`.
5. Copy the resulting flat `BuzzBridge.vst3` DLL into `dist-wine/`.

The output is a single file at `dist-wine/BuzzBridge.vst3`. yabridge accepts flat-file VST3 layout, so no `Contents/x86-win/` bundle directory is needed.

### Installing into yabridge

```bash
./scripts/build-linux-wine.sh --install
```

This runs the build and then invokes `yabridgectl add dist-wine` followed by `yabridgectl sync`, which generates the native Linux VST3 shim next to the Windows plugin. Rescan plugins in your DAW afterwards — `Buzz Generator Bridge` and `Buzz Effect Bridge` should appear.

To install manually:

```bash
yabridgectl add "$(pwd)/dist-wine"
yabridgectl sync
```

Or copy `dist-wine/BuzzBridge.vst3` into any directory `yabridgectl` is already watching (e.g. `~/.vst3/`, `~/.wine/drive_c/Program Files/Common Files/VST3/`).

### Installing from a release tarball

Download `BuzzBridge-Linux-vX.Y.Z.tar.gz` from the [Releases](https://github.com/nstarke/JeskolaBuzzVST/releases) page, extract, and run the bundled installer:

```bash
tar xf BuzzBridge-Linux-vX.Y.Z.tar.gz
cd BuzzBridge-Linux-vX.Y.Z
./installer.sh
```

The installer:

1. **Installs system dependencies.** It detects your package manager (`apt`/`dnf`/`zypper`/`pacman`) and offers to install WINE *with 32-bit support* plus `curl`/`unzip` (it prompts before running `sudo`). yabridge itself is not in most distro repos and is not auto-installed — install it from [its releases](https://github.com/robbert-vdh/yabridge) if `yabridgectl` is missing.
2. Copies `BuzzBridge.vst3` to `~/.local/share/BuzzBridge/`, registers it with `yabridgectl`, runs `yabridgectl sync`, and symlinks the generated shim into the top of `~/.vst3/` so non-recursive hosts (e.g. Renoise) find it.
3. **Sets up the WINE runtime** — creates a dedicated 32-bit (win32) prefix and writes the needed env vars to your shell rc. See [WINE 32-bit prefix and runtime](#wine-32-bit-prefix-and-runtime-environment) below.
4. Prompts to download the Buzz machine database (~30 MB, 726 machines) and extract it to `$HOME/Buzz/Gear`.

Options:

```bash
./installer.sh --deps             # install system packages without prompting
./installer.sh --no-deps          # skip the system dependency step
./installer.sh --wine-env         # write WINE runtime env to shell rc without prompting
./installer.sh --no-wine-env      # skip the win32-prefix / shell-rc step
./installer.sh --with-machines    # install machine DB without prompting
./installer.sh --no-machines      # skip machine DB download without prompting
```

Overrides: `BUZZVST_INSTALL_DIR` (plugin dir), `BUZZVST_WINEPREFIX` (win32 prefix, default `~/.wine-buzz32`), `BUZZVST_RC_FILE` (which rc file to edit), `BUZZVST_GEAR_DIR` (machine dir).

To uninstall (also removes the shell-rc env block; leaves the win32 prefix in place):

```bash
./uninstall.sh
```

### WINE 32-bit prefix and runtime environment

BuzzBridge is a **32-bit** Windows VST3, so yabridge runs it through its 32-bit host (`yabridge-host-32.exe`). That host has two hard requirements that a default desktop WINE setup often does not meet:

- **A 32-bit (win32) WINE prefix.** A normal `~/.wine` is a 64-bit (win64) prefix, and a 32-bit wineserver cannot drive it (`wine: '~/.wine' is a 64-bit installation, it cannot be used with a 32-bit wineserver`). The installer therefore creates a separate prefix at **`~/.wine-buzz32`** (override with `BUZZVST_WINEPREFIX`) and points the bridge at it via `WINEPREFIX`. Your main `~/.wine` is left untouched.
- **A 32-bit WINE loader, on new-WoW64 builds.** Recent distro WINE (e.g. Ubuntu's `wine-stable` 10.x) defaults to a 64-bit/WoW64-only `wine` that cannot launch the 32-bit *winelib* host at all (`wine: failed to start ...yabridge-host-32.exe.so`). When the installer detects this, it sets `WINELOADER` to the distro's 32-bit loader (e.g. `/usr/lib/i386-linux-gnu/wine/wine`). On WINE builds where the default `wine` already runs the 32-bit host, `WINELOADER` is left unset.

The installer writes these into a clearly marked block in your shell rc (`~/.bashrc`, `~/.zshrc`, or `~/.profile` depending on `$SHELL`):

```bash
# >>> BuzzBridge (yabridge 32-bit runtime) >>>
export WINEPREFIX="$HOME/.wine-buzz32"
export WINELOADER="/usr/lib/i386-linux-gnu/wine/wine"   # only if your wine is 64-bit/WoW64-only
export BUZZVST_GEAR_DIR='Z:\home\you\Buzz\Gear'
# <<< BuzzBridge (yabridge 32-bit runtime) <<<
```

It is **idempotent**: an existing BuzzBridge block is left untouched if unchanged, refreshed if its values changed, and skipped (with a warning) if you already export `WINEPREFIX`/`WINELOADER` yourself elsewhere in the file.

> ⚠️ **`WINEPREFIX`/`WINELOADER` are global.** Any shell that sources this rc file will use the 32-bit prefix/loader for *every* `wine` command, which can break other 64-bit Windows apps you run under WINE. If you use WINE for other things, remove the block (or run `./uninstall.sh`) and instead launch only your DAW with these vars set — for example via a small wrapper:
>
> ```bash
> #!/bin/sh
> exec env WINEPREFIX="$HOME/.wine-buzz32" \
>          WINELOADER=/usr/lib/i386-linux-gnu/wine/wine \
>          renoise "$@"
> ```

After the installer writes the rc block, open a new terminal (or `source` the rc file) before launching your DAW from a shell, then rescan plugins.

### Pointing at your Buzz machines

On startup, BuzzBridge auto-detects a Buzz Gear directory by checking the following locations in order:

1. `$BUZZVST_GEAR_DIR` (explicit override — any Windows or WINE-mapped path)
2. `%ProgramFiles(x86)%\Jeskola Buzz\Gear` (Jeskola installer default)
3. `%ProgramFiles%\Jeskola Buzz\Gear`
4. `%USERPROFILE%\Buzz\Gear` (legacy/portable install)
5. `%USERPROFILE%\Jeskola Buzz\Gear`
6. `Z:\home\$USER\Buzz\Gear` (WINE fallback — matches the Linux `install.sh` default)

Under WINE, `%ProgramFiles(x86)%` and `%USERPROFILE%` resolve through the WINE prefix. If you install Jeskola Buzz via its own installer into WINE, paths #2 and #4 both work with no extra configuration. If your Buzz machines live somewhere else (e.g. on your Linux home directory, accessed via WINE's `Z:\` drive mapping), set the env var before launching your DAW:

```bash
export BUZZVST_GEAR_DIR='Z:\home\alice\buzz-machines'
# then start your DAW
```

You can also override at any time from the plugin UI's "Load Buzz Machine..." browser, which takes any path WINE can see.

### Preset backup directory (optional)

BuzzBridge can mirror saved presets to a secondary backup directory. On Windows this defaults to `%USERPROFILE%\OneDrive\BuzzVST\` if OneDrive is present. To configure a custom location (required on Linux, where OneDrive doesn't exist under WINE), set:

```bash
export BUZZVST_BACKUP_DIR='Z:\home\alice\buzz-preset-backups'
```

If neither `BUZZVST_BACKUP_DIR` nor `%USERPROFILE%\OneDrive` resolves to an existing directory, preset backups are silently skipped and the primary `.prs` file beside each machine DLL is the only copy.

### Running the tests on Linux

The unit test suite builds and runs under WINE — it is a 32-bit Windows `.exe`
that, thanks to the static llvm-mingw runtime, needs no extra DLLs:

```bash
./scripts/build-linux-wine.sh --test
```

This builds `BuzzBridgeTests.exe` with the cross toolchain and runs all 630+
cases under WINE. To exercise real Buzz machine DLLs and produce a Linux
compatibility report (the counterpart of the Windows `machine-support_windows.txt`):

```bash
./scripts/run_machine_tests_linux.sh           # uses the bundled machine DB
./scripts/run_machine_tests_linux.sh <gear_dir>
```

The result table is written to `machine-support_linux.txt`.

### Crash protection on Linux

Under MSVC, BuzzBridge wraps every call into machine code in `__try`/`__except`
so a buggy machine can't crash the host. Clang's i686 SEH is unusable here
(broken assembler labels for templated lambdas), so the Linux build installs a
process-wide **Vectored Exception Handler** and `longjmp`s out of it back to a
per-call landing pad (see `src/common/SEHGuard.h`). `AddVectoredExceptionHandler`
is a plain Win32 API that works under WINE regardless of compiler, so a faulting
Buzz machine (access violation, illegal instruction, divide-by-zero, …) is
caught and the plugin survives instead of taking down the WINE host process.
Set `BUZZ_NO_VEH=1` in the environment to disable it. It is a first-chance
handler, so in the rare case a machine relies on its *own* internal
`__try`/`__except` to recover from a hard fault, ours preempts it; and a stack
overflow is deliberately left uncaught.

### vtable ABI fix (why audio works under the cross build)

Buzz machine DLLs are MSVC-compiled. Under the Microsoft C++ ABI a virtual
destructor occupies exactly **one** vtable slot, so a machine's vtable is
`[dtor, Init, Tick, Work, …]`. The Itanium C++ ABI that clang/GCC use on the
mingw target emits **two** slots for a virtual destructor (complete + deleting),
giving `[D1, D0, Init, Tick, Work, …]`. That one-slot shift meant the
cross-compiled host calling `pMachine->Init()` actually invoked the machine's
`Tick()`, `Work()` hit `WorkMonoToStereo()`, and so on — every machine inited
but produced **silence**. (Native Windows is unaffected: host and machine are
both MSVC, so both use one dtor slot.)

`CMachineInterface` in `MachineInterface.h` is therefore declared with a single
placeholder virtual in slot 0 and a non-virtual destructor on the clang-mingw
build, reproducing MSVC's layout so every method index lines up.
`BuzzMachineLoader::Unload()` deletes a machine through its own MSVC
scalar-deleting destructor at `vtable[0]` to compensate. With this fix the
Linux sweep matches Windows machine-for-machine; compare
`machine-support_linux.txt` against `machine-support_windows.txt`.

### Limitations

- **GUI chrome.** The "Load Buzz Machine..." file dialog uses Win32 `GetOpenFileName` and renders in WINE's default style rather than your Linux desktop theme. Functional but not native-looking.

## Running Tests

Tests run against the 32-bit build (where Buzz machine DLLs can be loaded directly):

```bash
cmake --build build32 --config Release --target BuzzBridgeTests
ctest --test-dir build32 -C Release --output-on-failure
```

Or run the test executable directly:

```bash
build32\bin\Release\BuzzBridgeTests.exe
```

The test suite includes 173 tests covering parameter layout, value mapping, oscillator tables, host callbacks, MIDI note/velocity/CC conversion, gear directory scanning, and integration tests that load real Buzz machine DLLs.

## Usage

1. Load **Buzz Generator Bridge** (as an instrument) or **Buzz Effect Bridge** (as an effect) in your DAW
2. Open the plugin editor -- click **"Load Buzz Machine..."** to browse for a Buzz machine DLL
3. The machine's parameters appear as automatable VST3 sliders
4. For generators, send MIDI notes to trigger sound -- velocity is automatically routed to the machine's volume parameter

## 64-bit Bridge Architecture

Buzz machine DLLs are 32-bit and cannot be loaded into a 64-bit process. BuzzBridge solves this with an out-of-process bridge:

```
64-bit DAW
  └─ BuzzBridge.vst3 (64-bit)
       │
       │ spawns on first use
       │
       └─ BuzzBridgeHost32.exe (32-bit child process)
            │
            └─ Buzz Machine DLL (32-bit)

  Communication:
    Named Pipe ──── commands, parameters, MIDI, state
    Shared Memory ─ audio buffers (zero-copy)
```

- The 64-bit plugin spawns `BuzzBridgeHost32.exe` as a hidden child process the first time a machine is loaded
- Commands (load DLL, tick, set parameters, MIDI events) are sent over a named pipe
- Audio data is transferred via shared memory for minimal latency
- The bridge host handles the 256-sample chunking required by Buzz machines internally
- If the bridge process crashes (due to a buggy machine), the DAW process is unaffected
- Each plugin instance gets its own bridge process and session

The 32-bit plugin variant loads Buzz machines directly in-process without the bridge, which has lower latency but requires a 32-bit DAW.

## MIDI Support

- **Note On/Off**: MIDI notes are converted to Buzz note format (`(octave << 4) | note`) and written to `pt_note` parameters. The machine's `MidiNote()` handler is also called directly.
- **Velocity**: MIDI velocity is automatically routed to the volume/velocity byte parameter adjacent to the note parameter, scaled to the parameter's range.
- **MIDI CC**: The controller implements `IMidiMapping`, mapping CCs sequentially to Buzz global parameters. DAWs can use this for MIDI Learn. CCs are also forwarded to machines that implement `CMachineInterfaceEx::MidiControlChange()`.
- **Poly Pressure / Aftertouch**: Routed to the machine's extended interface as aftertouch CC.

## Architecture

```
src/
  buzz/                         Buzz machine integration layer
    MachineInterface.h            Official Buzz SDK header (v66)
    BuzzMachineLoader.h/cpp       DLL loading and lifecycle
    BuzzCallbacks.h/cpp           CMICallbacks host stub
    BuzzOscTables.h/cpp           Bandlimited oscillator tables
    BuzzParamLayout.h/cpp         Packed parameter struct layout
  vst3/                         VST3 plugin layer
    BuzzProcessor.h/cpp           Base audio processor (tick timing, params, MIDI)
    GeneratorProcessor.h/cpp      Instrument variant (no input, stereo out)
    EffectProcessor.h/cpp         Effect variant (stereo in/out)
    BuzzController.h/cpp          Parameter controller + IMidiMapping
    BuzzPluginView.h/cpp          Native Win32 GUI (DLL file browser)
    ParameterMapping.h/cpp        Normalized <-> Buzz integer conversion
    BuzzPluginFactory.cpp         VST3 factory registration
  bridge/                       64-bit <-> 32-bit bridge
    BridgeIPC.h                   Shared memory + named pipe protocol
    BridgeClient.h/cpp            64-bit side: sends commands to host process
    BuzzBridgeHost32.cpp          32-bit host exe: loads machines, processes audio
  common/
    SEHGuard.h                    __try/__except crash protection
installer/                      Inno Setup installer script
tests/                          173 unit and integration tests
sdk/                            Steinberg VST3 SDK (git submodule)
```

## License

The Buzz machine interface header (`MachineInterface.h`) is Copyright (C) 1997-2014 Oskari Tammelin and may be used to write freeware machines for Buzz. The VST3 SDK is subject to the [Steinberg VST3 License](https://github.com/steinbergmedia/vst3sdk/blob/master/LICENSE.txt).
