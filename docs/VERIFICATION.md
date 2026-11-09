# Verifica della prima build macOS

24 ottobre 2026 — Mac Apple Silicon, Apple Clang 17, CMake 4.3.3, SDL 3.4.18, FFmpeg 9.0.2, Dear ImGui 1.93.0 docking.

## Modello

Test del modello separato dalla GUI: split con conservazione del source in, rifiuto delle sovrapposizioni sulla stessa traccia, precedenza di V2, trim valido/non valido, valori non finiti, undo/redo, ripple delete, salvataggio/riapertura con percorsi relativi e spazi/apostrofi, conservazione di gain e grade, rifiuto di file malformati.

## Pipeline multimediale

Fixture generata da FFmpeg: video testsrc2 960×540, 25 fps, tre secondi, audio sinusoidale 440 Hz. Verificati metadati, frame RGBA, seek avanti/indietro e fine file, mix PCM stereo, durata della timeline, audio presente sulle clip e silenzio nel vuoto, MP4 H.264/AAC, risoluzione esportata e nero nel vuoto. Export fallito e annullato non sostituiscono la destinazione preesistente.

Il transport viene avviato, fermato e riavviato con seek attraverso il driver audio dummy SDL. Il clock deve avanzare e la riproduzione deve terminare correttamente. Questa verifica non qualifica la latenza degli altoparlanti reali.

## Interfaccia

Finestra nativa avviata e osservata sul Mac. Verificati visivamente pannelli, tab e spazio ImNodes. Uno smoke test separato ha aperto `Demo.frp`, generato miniature e mostrato il video reale nella preview. Backend dichiarato a runtime: `metal`. Cattura in `build/checks/Editor.png`.

25 ottobre 2026 — Doppio viewer: prova isolata con sorgente rosso e timeline con pattern video. Verificati decoder e texture separati, passaggio fra Single viewer e Dual viewer, preferenza condivisa fra Cut ed Edit, seek e selezione nel Media Pool senza spostare l’altro monitor, cambio di riproduzione con audio dummy, grade applicato soltanto alla timeline, errori separati e vuoti della timeline. Verifica visiva della build Metal anche con timeline molto alta e controlli dei due monitor visibili. Configurazioni e media di prova in `build/checks/dual-viewer/`, separati dalla sessione utente.

25 ottobre 2026 — Compattazione del viewer: Single viewer e Dual viewer sono tab della stessa barra docking, con Guides sulla medesima riga. Conservato l’identificatore del viewer precedente per recuperare la suddivisione salvata. Eliminate intestazioni e nomi sopra i monitor; scrubber su linea sottile con pomello piccolo e controlli compatti. Verificati cambio di tab in Cut/Edit, posizione iniziale della preview senza righe intermedie, assenza di overflow verticale e seek indipendente dalla nuova barra. Catture Metal e configurazioni separate in `build/checks/compact-viewer/`.

25 ottobre 2026 — Slider minimali estesi a dimensione miniature, zoom logaritmico, gain in Inspector e Mixer, brightness, contrast e saturation. Disegno della linea e del pomello condiviso con lo scrubber; interazione basata su SliderBehavior di ImGui. Verificati ridimensionamento delle miniature senza dirty/undo del progetto, zoom logaritmico, gain e grade con undo, inserimento numerico con Cmd+clic su macOS e limiti durante il trascinamento fuori dal controllo. Verifiche isolate e catture in `build/checks/minimal-sliders/`.

26 ottobre 2026 — Deliver: esportati e riaperti H.264 con due bitrate, HEVC con qualità automatica e bitrate obiettivo, ProRes Proxy/LT/422/HQ in MOV. Verificati codec e profilo effettivi, tag hvc1 per HEVC, 10-bit 4:2:2 e audio PCM 24-bit per ProRes, stereo a 48 kHz, risoluzione, durata e nero nei vuoti con due tracce video. Il bitrate impostato modifica il flusso H.264. Verificati annullamento e fallimento senza sostituire la destinazione per HEVC e ProRes, rifiuto di bitrate non validi ed estensioni errate, pulizia dei temporanei. File in `build/checks/export-formats/`; catture UI in `build/checks/deliver-ui/`. CTest: 4/4 superati.

26 ottobre 2026 — Workspace semplificati: rimossa la pagina Cut dalla navigazione e dal menu Workspace. In Edit aggiunta Cut accanto a Timeline nella stessa barra docking; il pannello Cut è provvisorio. Conservati gli ID dei sei workspace rimasti e il precedente argomento `--page 1` reindirizza a Edit. Verificati nella build Metal il layout iniziale con Timeline selezionata, un layout preesistente con doppio viewer e la riapertura con Cut selezionato, in configurazioni isolate. Catture in `build/checks/edit-cut/`. Build riuscita e CTest 4/4 superati.

26 ottobre 2026 — Panoramica Cut: striscia compatta con scala temporale adattata alla larghezza, miniature proporzionate, nomi solo quando entrano e playhead condiviso con Timeline. Verificati visivamente nella build Metal una clip a tutta larghezza, clip consecutive di durate diverse, vuoti e sovrapposizioni V1/V2. Prova isolata delle interazioni ImGui: clic, selezione clip, aggiornamento del decoder timeline senza spostare il sorgente, trascinamento oltre il bordo con limite alla durata, ridimensionamento a 1100×700 e conservazione dello zoom, del progetto e della storia. Provato anche il montaggio vuoto. Catture e fixture in `build/checks/cut-overview/`. La timeline dettagliata di Cut resta da sviluppare.

26 ottobre 2026 — Cut su fila unica: proiezione delle tracce video in una sola striscia, con confini al centro delle sovrapposizioni parziali. Il montaggio reale e la priorità video rimangono invariati. Test dedicato per ordine delle tracce, sovrapposizioni concatenate, vuoti, clip contenute, clip nascoste e bordi coincidenti. Verificate nella UI selezione prima/dopo il centro, corrispondenza del seek ai tempi reali, ridimensionamento e assenza di modifiche al progetto. Catture Metal in `build/checks/cut-single-row/`. Build riuscita e CTest 5/5 superati.

26 ottobre 2026 — Media Pool e transizioni: controlli grafici elenco/griglia prima di Thumbnail size, griglia senza testo e modalità persistente. In Edit, Media Pool, Transitions e Text condividono la barra docking e recuperano i layout precedenti. Text è un pannello predisposto. Fade In/Out e Cross Dissolve modificano l’opacità video, con preview delle due tracce e dei relativi letterbox, indicatori nella timeline, undo/redo e persistenza `.frp` v2; lettura dei progetti v1 mantenuta. Cross Dissolve usa una sovrapposizione parziale esistente fra V1/V2 e non modifica l’audio. Verificati i colori dei file H.264/HEVC/ProRes, dissolvenza con i due ordini delle tracce, fade in/out, trim/split e salvataggio. Prova UI isolata: clic griglia, preferenza salvata, tab affiancati, pulsante Cross Dissolve, preview al centro della dissolvenza (RGB 132/0/122 da sorgenti rosso/blu), undo/redo e apertura Text. Catture in `build/checks/library-ui/`, fixture ed export in `build/checks/transitions/`. Build riuscita e CTest 6/6 superati.

26 ottobre 2026 — Cornici delle miniature: stesso colore verde per tutti gli elementi del Media Pool, spessore 2 px per quelli non selezionati e 4 px per quello selezionato, sia in elenco sia in griglia. Il bordo resta all’interno del riquadro senza modificarne le dimensioni e compare anche in attesa dell’immagine. Build compilata e verifica visiva Metal con miniature orizzontali e verticali; catture in `build/checks/thumbnail-borders/`.

26 ottobre 2026 — Correzione cornici e continuità dei tagli: bordo pieno esterno da 2/4 px, senza sovrapposizione sull’immagine e senza fondo pieno della card selezionata nella griglia. Verifica visiva Metal in elenco e griglia in `build/checks/thumbnail-outlines/`. Riprodotti su un export di prova a 320×180 del progetto di recupero a 30000/1001 fps un fotogramma nero prima del taglio a 16,7167 s e due prima/durante il taglio a 32,666 s. L’export usa ora offset e finestre di overlay in fotogrammi interi, con mantenimento dell’ultima immagine decodificata solo entro la durata della clip. Nel confronto prima/dopo blackdetect non rileva più i due stacchi; rimane soltanto il vuoto iniziale reale da 0 a 0,633967 s. Report in `build/checks/cut-continuity/black-frames.json`. Il test video_transitions copre i fotogrammi adiacenti ai tagli a 25 e 29,97 fps e il primo fotogramma dopo un vuoto, in H.264, HEVC e ProRes. La preview conserva una scena completa tramite texture a doppio buffer finché tutti i livelli richiesti sono pronti, distinguendo le clip anche quando usano lo stesso file. Prova UI isolata per tagli avanti/indietro senza nero, sostituzione atomica dei due livelli della dissolvenza, vuoti reali e media mancanti. Build riuscita e CTest 6/6 superati.

Il contatore FPS dell'interfaccia non è una misura delle prestazioni di montaggio o encoding. Non sono stati qualificati 4K multitraccia, HDR, RAW, progetti lunghi, distribuzione su altri Mac o driver Exec64.

## Riproduzione

```sh
bash scripts/build.sh
```

Sei test CTest: `project_and_editing`, `cut_overview`, `video_transitions`, `generate_media_fixture`, `media_pipeline`, `export_formats`. Le fixture e gli export di verifica sono confinati a `build/checks/`.
