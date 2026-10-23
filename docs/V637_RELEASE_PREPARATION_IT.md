# OFXR-BRIDGE 0.3.0 (v637) ? prova locale

Chiudere il vecchio tray e riavviare il gioco con il tray di questa cartella.
Questa build resta nel laboratorio: non e una release pubblicata.

- Tray Pacing: Auto, Force VDXR o Force SteamVR. Cambia il percorso alla
  prossima apertura del gioco; non cambia il runtime OpenXR installato.
- AFW Only e nel tab Luke Ross ed e salvato per gioco; richiede riavvio.
- Open folder apre `%LOCALAPPDATA%/ofxr-bridge`.
- INI principale: `ofxr-bridge.ini`; profili: `Profiles/<nome.exe>.ini`.
- Un nuovo gioco eredita la configurazione attuale dell INI principale.
  F10 e il pannello desktop collegato al gioco salvano solo il suo profilo.
  Senza gioco collegato, Settings modifica le impostazioni globali di partenza.
- Reset to defaults ripristina i valori della prima installazione, non quelli
  dell INI principale, e lascia intatta la tray.
- `AdvancedMode=0` nella sezione `[ofxr]` e il default. Con 1 ricompaiono
  Motion source, Clear before framewarp ed Experimental features.
- Enable benchmarks abilita pulsanti e misure. I CSV vengono sempre scritti
  nella cartella comune Benchmarks, anche senza Diagnostic logging.
- SteamVR preparation e solo nell INI principale: `[tray] steamvr_separate_queue=1`.

Restano da confermare nel visore avvio, impostazioni per gioco e le forzature
fra SteamVR e VDXR. Non serve rifare il benchmark completo per controllare i
profili: cambiare un valore, chiudere e riaprire lo stesso gioco e sufficiente.
