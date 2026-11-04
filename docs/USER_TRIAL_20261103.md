# Prova manuale — 3 novembre 2026

Apri BG3RTX, espandi Experimental, cambia un controllo e premi Save.
Le modifiche si applicano durante la partita. Nessuna modifica manuale agli INI.
Per i capelli usa Full RT geometry già attiva all'avvio; per RR attiva Ray Reconstruction.
Per l'acqua prepara RT water e Off-screen bounce light all'avvio (preset Ultra).
La nuova DLL host va caricata avviando il gioco una volta dopo l'aggiornamento.
Tutti gli esperimenti partono spenti; RR order parte da Before post (default).
Per provare After post spegni prima RR hair dither resolve.

| Nome nella GUI / chiave | Save e punto da guardare | Cambiamento da confrontare | Costo noto RTX 5090 |
|---|---|---|---|
| Strand hair in ray-traced lighting / `rt_hair_groom` | Dialogue1; ombre dei capelli su viso e collo | Fili al posto delle card per ombre, AO, GI e riflessi; richiede Hair + scalp e groom disponibili | Rispetto alle card native: RT -1,1–1,5 ms, GI -1,6–1,8 ms; refit +0,57 ms |
| RR hair dither resolve / `rr_hair_resolve` | Dialogue3; capelli sottili contro sfondo sfocato | Con Before post: confronta scintillio puntinato e dettaglio; può ammorbidire la sagoma | Non misurato |
| RR order / `rr_post` | Dialogue3; stessi capelli e profondità di campo | Before post (default, 1) oppure After post (0); confronta scintillio e sfocatura | Delta non misurato |
| Water camera correction / `rt_water_compose_shift` | S8; piscina, gira con Q/E | Controlla se i riflessi restano presenti durante la rotazione | Delta della correzione attuale non misurato |
| Water: hidden-surface reflections / `rt_water_hit_shade` | S8; piscina, oggetti fuori schermo o nascosti | Confronta illuminazione degli oggetti riflessi | +0,8–2,4 ms |
| Native hair + scalp: Hair + scalp / `rt_native_hair` + `rt_native_scalp` | Dialogue1/Dialogue3; capelli, attaccatura, viso e collo | Copertura dei capelli e del cuoio capelluto nei raggi RT | RT +2,3–4,5 ms; GI +3,4–6,9 ms |
| Retire unused hair snapshots / `rt_hair_pool_trim` | Dialogue3; poi cambia inquadratura | Recupero del pool; nessuna variazione visiva attesa | GPU non misurato; pool -11–42% nei test |
| Single-sample hair coverage / `rt_hair_cheap` | Dialogue1/Dialogue3; ombre fini su viso e collo | Copertura secondaria semplificata; verifica perdita di ombre o variazioni di luce | Guadagno entro il rumore; qualità non qualificata |

Ctrl+Shift+F9 spegne e riaccende tutti gli effetti RT, indipendentemente dalla scelta Experimental salvata.
Ctrl+Shift+F10 alterna l'ultimo controllo Experimental modificato e salvato; senza una scelta valida non fa nulla.
`ab_hotkey=0` disabilita entrambi i tasti; `ab_off` della coda resta il toggle generale.
Per Hair + scalp cambia entrambe le chiavi; per RR order alterna i due ordini.
Le dipendenze devono essere già attive; il tasto non cambia preset.
Il cambio A/B è temporaneo: Save o una rilettura delle impostazioni lo ripristina.
Il log riporta una riga `A/B: <key>=<value>` con tutte le chiavi modificate.
Non esiste un overlay di notifiche: nessun indicatore sullo schermo per 2 s.
`rt_hair_gi_mean` resta escluso dalla prova e nascosto nella GUI normale.
I costi sono riferimenti di scene diverse, non promesse; nessun nuovo test in gioco eseguito.
