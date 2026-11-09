# FrameRender

Editor video locale per macOS, scritto in C++17. Interfaccia compatta ispirata a Blender e spazi di lavoro Media, Edit, Fusion, Color, Fairlight e Deliver.

## Avvio

Dipendenze di sviluppo tramite Homebrew:

```sh
brew install cmake ninja pkgconf sdl3 ffmpeg
bash scripts/build.sh
bash scripts/run.sh
```

L'app viene generata in `build/FrameRender.app`. Il progetto è stato compilato e verificato su macOS Apple Silicon. Questa è una build di sviluppo: utilizza le librerie Homebrew presenti sul Mac. Bundle indipendente, firma, notarizzazione e distribuzione sono passaggi successivi.

## Primo montaggio

1. Importare video con **Cmd+I**, il pulsante Import o trascinandoli nella finestra.
2. Fare doppio clic su un elemento del Media Pool per aggiungerlo a V1, oppure trascinarlo sulla timeline. Il menu contestuale consente di aggiungerlo a V2.
3. **Space** avvia/ferma la riproduzione; frecce sinistra/destra avanzano per fotogramma. Shift+freccia sposta di un secondo.
4. Trascinare una clip per spostarla; trascinarne i bordi per modificare il trim. V seleziona lo strumento di selezione, B la lama. **Cmd+B** divide la clip selezionata al playhead.
5. Backspace elimina; Shift+Backspace esegue una cancellazione con chiusura del vuoto sulla stessa traccia. **Cmd+Z** / **Shift+Cmd+Z** annullano/ripristinano.
6. Salvare con **Cmd+S** in formato `.frp`. Le modifiche non alterano i video originali.
7. Aprire Deliver e scegliere H.264/H.265 in MP4 oppure Apple ProRes in MOV. Per H.264/H.265 scegliere qualità automatica o bitrate video obiettivo; per ProRes scegliere il profilo. L'esportazione usa una copia del progetto e prosegue in background.

I salvataggi usano il formato `.frp` versione 2, che conserva anche i fade; i progetti versione 1 continuano ad aprirsi. I progetti conservano i percorsi relativi dei media rispetto alla propria posizione. Spostando il progetto senza i media, i riferimenti possono interrompersi; il relink è ancora da implementare. I file devono rimanere disponibili sul disco.

## Funzioni di questa prima versione

- Importazione asincrona, metadati e miniature di video locali.
- Pulsanti grafici List view e Thumbnail view prima di Thumbnail size: elenco con informazioni oppure griglia di sole miniature, senza testo sotto le immagini. Le miniature hanno una cornice verde esterna, aderente al contorno senza coprire l’immagine, in entrambe le viste: 2 px normalmente, 4 px quando selezionate. Modalità e dimensioni sono condivise fra Media ed Edit e conservate alla riapertura.
- Dimensione delle miniature regolabile dal controllo Thumbnail size nel Media Pool, con valore iniziale compatto al 75%. La preferenza è condivisa fra Media ed Edit e conservata alla riapertura dell'app.
- Preview fino a 1280 pixel di larghezza decodificata con FFmpeg e visualizzata tramite texture SDL/Metal. Durante il cambio clip conserva l’ultima immagine completa finché è pronta la successiva; le dissolvenze aggiornano insieme i due livelli e i vuoti reali restano neri.
- Tab **Single viewer** e **Dual viewer** affiancati nella barra principale del pannello in Edit, senza una seconda riga di tab. Nella vista doppia il sorgente selezionato nel Media Pool rimane a sinistra e la timeline a destra, con seek e posizioni separati. Un clic su un monitor lo attiva per Space e frecce; il bordo verde indica quello attivo. Si riproduce un monitor alla volta. La modalità scelta è conservata alla riapertura. I monitor non hanno righe aggiuntive per etichette o nomi file; lo scorrimento usa una linea sottile con un piccolo pomello. Le guide si attivano dal pulsante Guides nella stessa barra principale o con clic destro sul monitor.
- In Edit, linguette Media Pool, Transitions e Text nella stessa barra del pannello. Transitions applica Fade In, Fade Out e Cross Dissolve alla clip selezionata, con durata dei fade regolabile, rimozione e undo/redo. Cross Dissolve usa l’intera sovrapposizione parziale già presente fra clip su V1 e V2; audio invariato. Preview e export supportano le transizioni video. Text contiene per ora il pannello dedicato, senza strumenti di titolazione.
- Due tracce video con audio stereo collegato. V2 copre V1; gli audio delle due tracce vengono miscelati.
- Miniature della timeline a tutta altezza, con proporzioni conservate durante il ridimensionamento. Il nome è centrato nelle clip video e audio e viene mostrato solo se entra per intero; nelle clip strette la miniatura è ritagliata orizzontalmente.
- Riproduzione basata sul clock dell'audio, seek, split, trim, spostamento con snapping, cancellazione, undo/redo.
- Gain e mute per clip, meter stereo.
- Slider minimali coerenti per miniature, zoom della timeline, gain e correzioni colore: linea sottile e pomello piccolo, valori numerici a lato quando lo spazio lo consente e al passaggio del mouse. I controlli numerici mantengono navigazione da tastiera e inserimento con Cmd+clic su macOS.
- Salvataggio tramite file temporaneo e rename; backup di recupero ogni 30 secondi quando il progetto è modificato.
- Correzioni SDR di brightness, contrast e saturation.
- Export H.264/AVC e H.265/HEVC in MP4 con audio AAC stereo a 192 kbps; qualità automatica oppure bitrate video medio obiettivo da 0,1 a 1000 Mbps, regolabile con slider minimale o Cmd+clic. Il bitrate reale varia con il contenuto.
- Export Apple ProRes in MOV con profili 422 Proxy, 422 LT, 422 e 422 HQ, video 10-bit 4:2:2 e audio PCM stereo 24-bit/48 kHz. Il profilo, la risoluzione e gli fps determinano il bitrate; non è un bitrate arbitrario in Mbps.
- Risoluzione del progetto, 1080p o 720p; vuoti neri, audio miscelato, avanzamento e annullamento. Il dialogo di salvataggio e il file temporaneo seguono il codec selezionato. Un export fallito/annullato conserva il file di destinazione eventualmente già esistente.
- Layout docking, riordinabili e persistenti per i sei spazi di lavoro.

La timeline rimane agganciata allo spazio di lavoro. Per modificarne l'altezza trascinare il separatore orizzontale sopra il pannello: il viewer ridimensiona il video mantenendolo interamente visibile, insieme ai controlli di riproduzione. Le vecchie disposizioni con timeline flottante vengono riparate all'apertura, conservando le altre suddivisioni dei pannelli.

Fusion include inizialmente l'editor ImNodes con Media In → Media Out e preview pass-through. I nodi non costituiscono ancora un motore di compositing. Edit raccoglie il montaggio in un unico workspace: le linguette Timeline e Cut sono affiancate nella stessa barra del pannello. Timeline mantiene gli strumenti attuali; Cut mostra una panoramica sottile dell’intero montaggio, sempre adattata alla larghezza del pannello. La panoramica usa una sola fila: nelle sovrapposizioni parziali il confine visivo è posto al centro dell’intervallo comune, senza cambiare i tempi reali. Restano visibili i vuoti effettivi; per inserti contenuti in altre clip si conservano entrata e ritorno, con priorità alla traccia superiore quando una clip è interamente coperta. Clic e trascinamento spostano il playhead e attivano il viewer della timeline. La scala della panoramica è indipendente dallo zoom della timeline principale. La timeline dettagliata con gli strumenti di taglio verrà aggiunta successivamente.

## Limiti attuali

- Nessuna parità funzionale con DaVinci Resolve: niente titoli, transizioni avanzate, velocità variabile, proxy, waveform, automazioni audio, LUT, scopes o compositing a nodi eseguibile.
- Preview SDR non calibrata. Le correzioni CPU della preview sono approssimative; l'export usa i filtri FFmpeg. HDR e gestione colore professionale richiedono una pipeline dedicata.
- Il rendering dell'interfaccia è accelerato da Metal. Il decoder preview e gli encoder H.264, H.265 e ProRes di questo prototipo lavorano via software; VideoToolbox resta da integrare.
- L'audio di preview è prodotto da un processo FFmpeg isolato e trasmesso a SDL in piccoli buffer. Serve a validare il flusso; startup, latenza e scrubbing audio andranno ottimizzati con decoder persistenti.
- Non sono ancora qualificati video RAW, VFR, anamorfici, HDR, file danneggiati, contenuti remoti o progetti lunghi. Timecode non-drop-frame.
- Fino a 128 clip per esportazione. Sovrapposizioni sulla stessa traccia non consentite; l'audio resta collegato alla clip video.
- Solo macOS è attualmente abilitato. Il port Exec64 e quello Linux non fanno parte di questa tranche.

Layout e file di recupero si trovano in `~/Library/Application Support/FrameRender/`. Per riaprire un backup usare File → Open project e selezionare `Recovery.frp`. Il backup è singolo e viene sostituito dai successivi autosalvataggi; non sostituisce il salvataggio del progetto.

## Struttura

```text
src/core/       Progetto, clip, validazione, editing, storia e persistenza; nessuna dipendenza GUI
src/media/      Decoder, worker preview, processi, audio transport ed export
src/platform/   Dialoghi e integrazione macOS
src/ui/         Spazi di lavoro e timeline ImGui
external/       Sorgenti upstream fissati di Dear ImGui e ImNodes
tests/          Verifiche di editing e pipeline multimediale con fixture generate
```

VirGL riguarda il futuro backend grafico su Exec64. Il port dovrà qualificare separatamente presentazione, input, accesso ai file, codec, audio, processi e threading. La presenza di ImGui/ImNodes non rende già disponibile il motore video su Exec64.

## Verifiche

```sh
ctest --preset macos
```

La suite comprende sei test. Le verifiche multimediali generano un video di tre secondi con pattern e tono in `build/checks/`, verificano seek avanti/indietro, split e vuoti, audio PCM, export, cancellazione e conservazione delle destinazioni preesistenti. Il test dei formati verifica H.264 con bitrate differenti, H.265 nei due modi di controllo, il tag hvc1, tutti e quattro i profili ProRes, video 10-bit 4:2:2 e audio PCM 24-bit, validazione delle impostazioni e pulizia dei temporanei. Il test delle transizioni verifica i colori effettivi dei fade e della dissolvenza nei file H.264, H.265 e ProRes, i due ordini delle tracce, persistenza, undo, trim/split e compatibilità con i progetti precedenti. Il test della panoramica verifica confini a metà sovrapposizione, sequenze su tracce alternate, vuoti, inserti contenuti e conservazione del montaggio e della precedenza video reale. Il transport viene provato con il dispositivo audio dummy di SDL, senza riproduzione dagli altoparlanti. I test del modello verificano anche storia, trim, sovrapposizioni, ripple delete, persistenza e rifiuto dei progetti malformati.

Con l'app chiusa, per aprire il progetto di prova generato dai test:

```sh
open build/FrameRender.app --args --open "$PWD/build/checks/Demo.frp"
```

Il binario accetta anche `--page N` (0 Media, 2 Edit, 3 Fusion, 4 Color, 5 Fairlight, 6 Deliver; il precedente valore 1 apre Edit), `--media percorso`, e, per smoke test, `--smoke-frames N --screenshot percorso.bmp`. La variabile `FRAMERENDER_CONFIG_DIR` consente ai test UI di usare una cartella di configurazione separata da quella dell'utente.
