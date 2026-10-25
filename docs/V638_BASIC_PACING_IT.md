# OFXR-BRIDGE 0.3.0 (v638) - prova locale

Chiudere il vecchio tray e riavviare il gioco con il tray di questa cartella.

- Pacing normale: Framework oppure Director.
- Framework+director compare solo con `[ofxr] AdvancedMode=1` nel profilo.
- Un vecchio valore Framework+director viene riportato a Director in modalita
  normale; non rimane attivo di nascosto.
- Test normali: 21 senza vettori, 33 completi, 27 Luke Ross.
- Test Advanced: 31 senza vettori, 49 completi, 39 Luke Ross.
- VDXR standard resta Framework, con i suoi test 11 / 17 / 15 invariati.
- Colonne del foglio invariate: i test FW+Dir saltati ricevono n.a.
- Dieci secondi per test, primi tre esclusi; CSV sempre scritto se i benchmark
  sono abilitati, anche senza Diagnostic logging.

Restano i profili `Profiles/<nome.exe>.ini` nella cartella comune
`%LOCALAPPDATA%/ofxr-bridge`. I nuovi giochi ereditano l'INI principale;
Reset to defaults usa invece i valori di fabbrica. Le opzioni tray sono globali.

Build locale, senza commit o push e senza installazione automatica.
Verifiche automatiche completate come riportato nel file di validazione;
la conferma nel visore resta da fare.
