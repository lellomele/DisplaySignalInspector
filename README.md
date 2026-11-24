[Italiano](README.it.md) | [English](README.md)

# Display Signal Inspector

Portable Windows utility for checking what signal an external monitor receives. The desktop can be set to 2560×1440 while the HDMI output still runs at 3840×2160. The app shows both values.

## Use

Download [DisplaySignalInspector.exe](https://github.com/lellomele/DisplaySignalInspector/releases/latest/download/DisplaySignalInspector.exe) and run it on Windows 10 or 11 x64 with an external display active. The interface is in Italian.

1. Select the monitor. **Scansiona modalità** reads its EDID and checks five output presets with the driver, leaving the picture unchanged.
2. The active signal comes first. Other validated signals follow in bold. Desktop modes reported by Windows are listed separately, since they can share a source in Duplicate mode.
3. **Verifica** checks one preset. **Prova** temporarily applies it. Select **Conferma** within 15 seconds to keep the verified signal, or **Ripristina** to restore the previous configuration.

Presets cover Full HD at 60 Hz, QHD and 4K UHD at 30 and 60 Hz. EDID detection depends on the timing formats decoded. A driver-validated mode still needs a trial to confirm the actual output.

## Build

With MinGW-w64 GCC, G++ and windres on PATH, run `./build.ps1 -Test` in PowerShell. The executable is written to `dist`.

Copyright © 2026 Prof. ing. Raffaele Mele.
