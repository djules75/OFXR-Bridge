## V482 candidate: Horizon rolling flicker capture

V481 startup pass-through retained. Ctrl+F8 arms/stops the default-OFF buffer; each F8 saves the previous second and keeps recording, repeatable. Per-eye images/source IDs, AFW contracts and actual OpenXR poses accompany each snapshot. Focused29/29 pass. See docs/V482_LUKE_RING.md (V482_LUKE_RING.md within docs). Capture is diagnostic overhead; visible flicker remains unresolved. V474 fallback; sealed predecessors retained.

## V481 candidate: Horizon startup pass-through

Menus/loading pass until original AER production advances; gameplay duplicate filtering remains. Focused28/28 pass. See docs/V481_LUKE_STARTUP.md (V481_LUKE_STARTUP.md within docs). Owner confirms V480 still flickers: clock defect is absent, but GPU/readback gaps remain. V481 only fixes startup black output; F8 paired image/guide capture is needed for pixel/pose diagnosis. V474 fallback; all sealed predecessors retained.

## V480 candidate: Horizon clock continuity

Fix HFW producer prediction rebasing backward after physical output; fresh-only admission and V479 warm bypass retained. Exact V479 negative reproduces1ns, fix preserves21ms elapsed; focused27/27 pass. See docs/V480_LUKE_CLOCK.md (V480_LUKE_CLOCK.md in docs). Headset flicker validation pending; GPU/readback stalls remain. V474 fallback, V479 preserved. Earlier pointers below are historical.

## V479 candidate: Horizon fresh-content pacing

Pinned HFW AER source admission in all four AFW/OFXR states, no retained output repeats, unique-output FPS and both-Off warm GPU capture bypass. See docs/V479_LUKE_PACING.md (V479_LUKE_PACING.md within docs). Focused26/26 pass; Horizon fluidity/FPS recovery awaits owner run. V474 fallback, V478 preserved; earlier pointers below are historical.

## V478 candidate: Horizon parity with working OptiScaler V33018

HFW physical Forward contract restored selectively; game eye phase/provider preserved. F8 image capture, F9 live logging, F10 menu, Ctrl+Alt+F6 synchronized AFW debug. See docs/V478_LUKE_PARITY.md (V478_LUKE_PARITY.md from docs). Focused24/24 pass; actual headset parity pending. V474 fallback; V475/V476 excluded. Earlier candidate pointers below are historical.

# V473: selezione display Elliottate

Chiudi il gioco e il vecchio tray, estrai lo ZIP completo e avvia la473, poi il gioco. In Display seleziona Elliottate's stick to bottom: Applying settings deve terminare e apparire il pannello in basso al posto del numero. Prova poi Elliottate's follow controller, un angolo e Off durante la stessa sessione. Sul controller gira il polso oltre120gradi per almeno250ms; raddrizzandolo per almeno500ms il pannello si nasconde. Senza tracking valido resta nascosto, senza ripristinare automaticamente il numero agli angoli. Le scelte restano salvate e la generazione non viene riavviata dal solo cambio display. Non serve abilitare logging. FSR e tutte le modifiche V472 sono mantenute. Conferma nel visore ancora richiesta; i test automatici verificano il percorso reale di applicazione e rendering D11/D12. Le sezioni seguenti sono storico.

# V471: meter Elliottate e auto arm

Chiudi il gioco e il vecchio tray, estrai lo ZIP completo e avvia la471: il tray si arma automaticamente (FG resta secondo la preferenza salvata). In Display scegli Elliottate's stick to bottom per il pannello fisso in basso oppure Elliottate's follow controller per la stessa grafica sul controller girato. Gira il polso oltre120gradi per almeno250ms, poi raddrizzalo per almeno500ms per nasconderlo. I contatori e le due righe Vectors/Status sono gli stessi della GUI; logging facoltativo. Puoi cambiare tra le sei posizioni e Off durante la sessione. Per controller senza tracking o binding compatibili il pannello sul polso resta nascosto; usa quello fisso. Verifica dimensioni/leggibilita e confronto con F10. Djules ora riporta medium latency. Disable/Exit e cleanup restano quelli esistenti. --settings-only/--settings-preview non attivano il tray. Le sezioni precedenti sono storico delle build sigillate.

# V470: Cycle estimated

Chiudi gioco e vecchio tray, estrai tutto lo ZIP e avvia la470. Nella stessa scena di Village confronta0.2.1 e Djules tenendo il pannello visibile: Cycle length resta misurato, Cycle estimated sostituisce Presentation estimated ed aggiunge la stima degli slot del visore senza contare due volte le attese gia trascorse. N/A senza generato confermato o periodo noto. Non misura i fotoni/display effettivo; assume che il runtime mostri output distinti. Logging non necessario per il contatore. Se fai una run con log, controlla limite256MB; il tray mantiene il valore della precedente configurazione. FSR e tutte le route mantengono la generazione precedente. Nessuna nuova installazione nei giochi necessaria.

# V469: Stellar Blade DLSS-G

Chiudi il vecchio tray e il gioco, estrai lo ZIP completo e avvia la469. Prova la stessa scena e modalita UEVR Sequential della468, ruotando il mouse e il casco. Per il confronto iniziale seleziona DLSS Frame Generation e Generated resolution100%, come consigliato upstream per Stellar Blade. Mantieni uguali R19 e gli altri settaggi. Puoi confrontare anche OFXR Game motion vectors e Hybrid: la composizione R22 si applica a queste route. FSR mantiene il percorso precedente. Attiva il logging se vuoi registrare la run; la correzione funziona anche senza logging. La modalita opzionale di camera fitting R23 resta fuori da questo port mirato.

## Current owner scope: V464 FSR release-bound guides

V464 inherits sealed V463. The owner explicitly authorizes the targeted ordinary native-D3D12 FSR guide/eye association correction described in [V464 scope](docs/V464_FSR_RELEASE_GUIDES.md). This supersedes historical FSR preservation instructions only for that association. FSR provider, shader math, SDK camera/motion parameters and scheduling remain unchanged. OFXR, native DLSS-G, D11 and Luke/AFW contracts are retained. The owner additionally requests a simpler Hybrid menu: fallback is fixed to FidelityFX and the duplicated fallback controls are removed. R19 stays live and default OFF; Vulkan native guide ingress stays suspended. Game/headset confirmation of the intermittent MSFS/Returnal flicker is pending.

## Current owner scope: V463 Hybrid

V463 inherits sealed V462 and adds selectable Hybrid between OFXR and FSR. R19 camera remains default OFF and live on every route. Existing OFXR/FSR/DLSS-G routes retain their behavior. Hybrid has its own profile and optical-flow fallback; native Vulkan guide capture remains suspended. See docs/V463_HYBRID.md. Earlier no-R19/no-Hybrid constraints below are historical and superseded only by these explicit V462/V463 owner requests.

### Hybrid nella V464

Il fallback Hybrid usa sempre FidelityFX: nessun altro selettore e nessun blocco duplicato di qualit?/risoluzione/bidirezionale. Anche un profilo V463 che aveva salvato NVIDIA viene migrato a FidelityFX; le impostazioni del normale menu OFXR rimangono proprie.

### Confronto V464

Chiudi il vecchio tray e il gioco, estrai tutto lo ZIP V464 e avvia il nuovo tray. Per il confronto iniziale lascia R19 spento e le altre impostazioni uguali alla run V460. Prova FSR dal primo avvio e durante la rotazione del casco, poi FSR -> DLSS-G -> FSR. Attiva logging prima di avviare il gioco: gli eventi presenter_transition 751/752 mostrano watermark della release, pubblicazione guida e identit? stream/epoch verificata. Se entrambi gli occhi sono valutati prima delle release e non esiste un'identit? verificata o un offset che li distingua, FSR resta in bypass (stato guide temporal_mismatch). Questa build non dichiara risolta la qualit? visiva prima della prova sul gioco.

# Current V462 scope

R19 is an explicitly authorized, default-OFF live checkbox for all FG routes. See [V462 scope](docs/V462_R19_CAMERA.md). This supersedes older R19 exclusions below. Base V461; Vulkan guide capture remains suspended. No Hybrid yet. Game/headset validation pending.

# V461: interfaccia V460 e Vulkan come V457

Questa build parte dalla V460 e porta solo la sospensione della lettura dei vettori
Vulkan introdotta nella V457. FSR su D11/D12 resta invariato.

Chiudi il vecchio tray e il gioco, estrai tutto lo ZIP e avvia OFXRBridgeTray.exe.
Riapri NMS con le stesse impostazioni del confronto V457. OFXR deve usare optical
flow; le route Vulkan che richiedono i vettori restano in bypass. La nuova interfaccia
deve indicare il rilevamento Vulkan e la sospensione dei vettori.

Confronta FPS e laggate, prima con FG spento e poi con una sola route OFXR attiva.
Le laggate residue della V457 non sono dichiarate corrette da questa integrazione.
Logging e contatore restano disponibili; il modello NR rimane esterno allo ZIP.
