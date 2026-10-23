# OFXR-BRIDGE 0.3.0 (v640) - prova locale

In Display resta Auto pick best one, spento di default e salvato per gioco.
Nuove priorita, applicate solo alla scelta finale:

- Luke: 2x2 ammesso da 3000 pixel su almeno un lato per occhio; 4x4 da 4000.
  Conta la risoluzione XR effettiva per occhio, non i due occhi affiancati.
- Prima si preferisce AFW solo se raggiunge almeno l'80% degli Hz del casco.
- Nel gruppo rimasto si preferisce 1x1 se raggiunge almeno il 90% degli Hz.
- A FPS equivalenti (entro 1 FPS) si preferiscono i vettori confermati anche
  con fino a 12 ms di latenza in piu. Poi valgono gli spareggi di stabilita.
- FidelityFX resta escluso dalla classifica automatica, ma compare nei test.

Esempio a 90 Hz: AFW solo ha priorita da 72 FPS; 1x1 da 81 FPS.
Il meter Elliott mostra AFW solo in verde quando sta effettivamente lavorando.
Benchmark, colonne del foglio e tempi di generazione/consegna invariati.
Dettagli aggiuntivi nel CSV details: risoluzione, Hz e conferma vettori.

Chiudere il vecchio tray e riavviare il gioco con quello di questa cartella.
Build solo locale, senza commit, push o installazione automatica.
