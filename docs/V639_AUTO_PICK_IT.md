# OFXR-BRIDGE 0.3.0 (v639) - prova locale

Chiudere il vecchio tray e riavviare il gioco con quello di questa cartella.
In Display, nella sezione benchmark, compare **Auto pick best one**.

- Spento di default, salvato per gioco.
- A fine benchmark considera i risultati entro 1 FPS dalla mediana massima.
- Fra questi considera quelli entro 2 ms dalla latenza mediana minima.
- Preferisce poi l'intervallo min-max degli FPS piu stretto, quindi quello
  della latenza. Usa i valori non arrotondati.
- FidelityFX resta nei test e nel CSV ma e escluso dalla selezione automatica.
- Applica e salva la combinazione vincente, compresi pacing, Fast, 2X/3X
  e qualita AFW per Luke. Il messaggio finale indica scelta e misure.
- Stop, errori o risultati insufficienti non applicano una scelta parziale.
- Con checkbox spento, alla fine torna alle impostazioni iniziali.

Valido anche per VDXR standard e Luke Ross. Numero dei test e colonne del CSV
invariati rispetto alla v638. Nessun cambiamento al rendering o al pacing.
Build solo locale: nessun commit, push o installazione automatica.
