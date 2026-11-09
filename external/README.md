# Sorgenti di terze parti

Le librerie GUI sono vendorizzate senza modifiche al codice upstream. Nessuna dipendenza dal repository Exec64 durante la compilazione.

| Libreria | Versione / commit | Origine | Licenza |
| --- | --- | --- | --- |
| Dear ImGui | v1.93.0-docking / b916186e66aa3c02b72c6ca17455516512ed8049 | https://github.com/ocornut/imgui | `imgui/LICENSE.txt` |
| ImNodes | eb36902c892548ef94f88f51ad7e7c9c7058a71c | https://github.com/Nelarius/imnodes | `imnodes/LICENSE.md` |

Dear ImGui usa il ramo docking; la coppia con ImNodes è verificata dalla build di FrameRender. Il port Exec64 usa attualmente un'altra revisione di ImGui: i futuri aggiornamenti devono verificare nuovamente la compatibilità della coppia.

SDL3 e FFmpeg sono dipendenze esterne installate con Homebrew, con le proprie licenze e configurazioni. La build FFmpeg usata per lo sviluppo include libx264. Per distribuire l'app occorre definire e verificare anche il pacchetto delle dipendenze e i relativi obblighi di licenza.
