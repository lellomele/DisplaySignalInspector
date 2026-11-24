[Italiano](README.it.md) | [English](README.md)

# Display Signal Inspector

Utility portatile per Windows che mostra il segnale ricevuto dal monitor esterno. Il desktop può essere impostato a 2560×1440 mentre l’uscita HDMI trasmette a 3840×2160. L’app mostra entrambi i valori.

## Uso

Scarica [DisplaySignalInspector.exe](https://github.com/lellomele/DisplaySignalInspector/releases/latest/download/DisplaySignalInspector.exe) e avvialo su Windows 10 o 11 a 64 bit con uno schermo esterno attivo.

1. Seleziona il monitor. **Scansiona modalità** legge il suo EDID e verifica cinque segnali con il driver, senza cambiare l’immagine.
2. Il segnale attivo compare per primo. Seguono in grassetto gli altri segnali convalidati. Le modalità desktop di Windows sono elencate separatamente perché in Duplica possono appartenere alla sorgente condivisa.
3. **Verifica** controlla un singolo segnale. **Prova** lo applica temporaneamente. Premi **Conferma** entro 15 secondi per mantenere il segnale verificato o **Ripristina** per tornare alla configurazione precedente.

I segnali disponibili sono Full HD a 60 Hz, QHD e 4K UHD a 30 e 60 Hz. Il rilevamento EDID dipende dai formati di temporizzazione decodificati. La prova consente di controllare il segnale effettivo dopo la validazione del driver.

## Compilazione

Con GCC, G++ e windres di MinGW-w64 nel PATH, esegui `./build.ps1 -Test` in PowerShell. L’eseguibile viene creato in `dist`.

Copyright © 2026 Prof. ing. Raffaele Mele.
