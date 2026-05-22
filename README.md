# Relazione Progetto C3 — Simulatore Multi-Servente

## 1. Introduzione

Il progetto richiede la realizzazione di un simulatore a eventi discreti
di un sistema composto da `N` serventi indipendenti operanti in parallelo,
ciascuno dotato della propria coda FIFO infinita. Gli arrivi al sistema
seguono un processo di Poisson di tasso `λ`; il servente `i`-esimo eroga
servizio con tempi esponenziali negativi di valor medio `1/μ_i`. Un
pacchetto in ingresso viene assegnato a una delle `N` code secondo una
politica di instradamento selezionabile fra:

1. **Random** — scelta uniforme tra le `N` code;
2. **Round-robin** — assegnazione ciclica deterministica;
3. **Shortest queue (JSQ)** — il pacchetto va nella coda con meno
   pacchetti presenti (in caso di parità, scelta casuale tra le code a
   pari lunghezza minima).

Le metriche di interesse sono:

- tempo medio di permanenza nel sistema `W` (sojourn time, calcolato su
  tutti i pacchetti serviti nella finestra di misurazione);
- lunghezza media di ogni coda `L_i`, `i = 0, …, N-1`;
- indice di sbilanciamento `I = max_i L_i − min_i L_i`.

L'obiettivo è confrontare le tre politiche di instradamento al variare
di carico, eterogeneità dei serventi e numerosità `N`.


## 2. Architettura del simulatore

Il simulatore estende una codebase C++ a eventi discreti preesistente,
originariamente sviluppata per il modello G/G/1. Le entità riusate senza
modifiche significative sono:

| Componente | File              | Ruolo                                      |
|------------|-------------------|--------------------------------------------|
| `simulator`| `simulator.{h,c}` | Classe astratta base                       |
| `calendar` | `calendar.{h,c}`  | Event list ordinata per tempo crescente    |
| `buffer`   | `buffer.{h,c}`    | Coda FIFO di `packet`                      |
| `packet`   | `packet.{h,c}`    | Pacchetto con istante di generazione       |
| `Sstat`    | `stat.{h,c}`      | Statistiche con intervalli di confidenza   |
| `rand.h`   | `rand.h`          | Macro per generazione esponenziale/uniforme|

### 2.1 File modificati per il progetto C3

In accordo con la consegna ("inviare via mail i file modificati .cpp /
.h"), tutte le estensioni sono state introdotte direttamente nei file
preesistenti della codebase:

| File              | Modifiche                                                      |
|-------------------|----------------------------------------------------------------|
| `queue.h`         | Aggiunto `enum routing_policy`, vettori `mu`, `queues`, `busy`, `lenq`, `last_t`, `area`, contatori sojourn, metodi `route_packet()`, `update_area()`, `schedule_service()`, `schedule_arrival()`, modalità batch `set_batch()` / `results_csv()`. Rimosse le strutture single-buffer del vecchio G/G/1. |
| `queue.c`         | Riscritta la logica per gestire `N` serventi indipendenti. `input()` raccoglie `N`, `λ`, gli `N` valori `μ_i`, la politica di routing e i tempi di transitorio/misurazione. `run()` esegue una fase di warm-up e una di misurazione con accumulatori tempo-pesati per `L_i`. `results()` stampa le tre metriche richieste dalla traccia. |
| `event.h`         | `arrival` e `service` ora referenziano la classe `queue` (puntatore al sistema multi-coda) invece del singolo `buffer*`. `service` trasporta in più l'indice `srv_id` del servente che completa il servizio. Distruttore base reso `virtual`. |
| `event.c`         | `arrival::body()` invoca `queue::route_packet()` per scegliere la coda di destinazione e schedula il prossimo arrivo. `service::body()` gestisce il completamento del servente `srv_id`, aggiornando gli accumulatori. |
| `main.c`          | Aggiunto dispatch fra modalità interattiva (default) e batch (`--batch` con tutti i parametri da CLI), con stampa dei risultati in CSV per il batch. |
| `Makefile`        | Aggiunto target ausiliario `runner` per gli esperimenti della relazione; abilitato `-Wall -Wextra` con soppressione mirata dei warning legacy dei file `.c` non toccati (`-Wno-deprecated`, `-Wno-writable-strings`, `-Wno-unused-but-set-variable`). |

L'unico file *aggiunto* al progetto è `runner.cpp`: si tratta di
**tooling ausiliario** per la relazione (orchestra le simulazioni della
sezione 4 e produce i PNG), non parte del simulatore. Invoca
`./queue --batch ...` via `popen()`, raccoglie le righe CSV, scrive i
file `scenarioA/B/C.csv` e poi pilota `gnuplot` (sempre via pipe) per
generare i sei grafici PNG nella sottocartella `grafici/`.

### 2.2 Strutture dati principali

Lo stato della classe `queue` comprende, per ciascuno degli `N` serventi:

```cpp
std::vector<buffer*> queues;   // code FIFO indipendenti
std::vector<int>     busy;     // 0 = idle, 1 = servente occupato
std::vector<int>     lenq;     // pacchetti nel sistema-servente
std::vector<double>  last_t;   // ultimo istante di aggiornamento area
std::vector<double>  area;     // integrale di lenq(t) dt
```

`lenq[i]` conta i pacchetti totali presenti al servente `i`, ossia la
somma di quelli in coda di attesa e dell'eventuale pacchetto in
servizio. L'**event list** è una singola `calendar* cal` globale e
condivisa fra tutti i serventi: ogni `service` referenzia il
proprio `srv_id`, ma tutti gli eventi sono ordinati per tempo nella
stessa struttura.

### 2.3 Politiche di instradamento

La politica è selezionata da un `enum routing_policy { POL_RANDOM,
POL_ROUND_ROBIN, POL_SHORTEST }` definito in `queue.h` e implementata
in `queue::route_packet()`:

- `POL_RANDOM` — un `GEN_UNIF(SEED, 0, N-1, idx)` produce un indice
  uniformemente distribuito;
- `POL_ROUND_ROBIN` — un contatore `rr_next` viene incrementato modulo
  `N` a ogni arrivo;
- `POL_SHORTEST` — scansione lineare di `lenq[]` per trovare il minimo;
  in caso di pareggio, gli indici a pari lunghezza minima vengono
  raccolti in un vettore e si estrae uniformemente fra di essi.

### 2.4 Raccolta delle statistiche

- **Sojourn time medio** — accumulato per somma incrementale. Quando un
  `service` libera il pacchetto in testa alla coda, si somma
  `time − packet->get_time()` all'accumulatore `sojourn_sum` e si
  incrementa `pkt_done`. La media è `sojourn_sum / pkt_done`.
- **Lunghezza media per coda** — calcolata tramite accumulatore
  **tempo-pesato**: a ogni arrivo o partenza si aggiorna `area[i] +=
  lenq[i] · (now − last_t[i])` *prima* di modificare `lenq[i]`. Al
  termine della simulazione `L_i = area[i] / T`, dove `T` è la durata
  della fase di misurazione.
- **Indice di sbilanciamento** — semplicemente `max(L_i) − min(L_i)`.
- **Transitorio** — un periodo iniziale di durata configurabile (warm-up)
  durante il quale gli eventi vengono processati ma gli accumulatori
  vengono azzerati al termine del transitorio. La fase di misurazione
  parte dunque da uno stato "tipico" del sistema.


## 3. Assunzioni

Le scelte implementative non vincolate esplicitamente dalla traccia sono
state le seguenti:

1. **Definizione di "lunghezza di coda".** Per `L_i` si è scelto di
   contare *tutti* i pacchetti presenti al servente `i`, ossia quelli in
   coda di attesa **più** quello eventualmente in servizio. La
   definizione si applica coerentemente sia alla politica JSQ (che
   confronta il "carico" totale di ciascun servente) sia all'accumulatore
   tempo-pesato per `L_i`.
2. **Coda iniziale a `t = 0`.** Il primo `multi_arrival` viene schedulato
   esattamente a `t = 0`; gli inter-arrivi successivi sono esponenziali
   con media `1/λ`.
3. **Sorgente di numeri pseudocasuali.** Si usa un unico stream globale
   (`SEED = 2`, definito in `global.h`) per inter-arrivi, tempi di
   servizio e decisioni di routing. La generazione esponenziale viene
   effettuata con la macro `GEN_EXP` preesistente; quella uniforme intera
   con `GEN_UNIF`.
4. **Politica JSQ con pareggi.** In caso di più code di pari lunghezza
   minima, la scelta è effettuata uniformemente fra gli indici legati,
   come previsto dalla traccia.
5. **Convergenza statistica.** Il numero di pacchetti simulati non è
   fisso a priori: si configura la durata simulata del transitorio e
   della fase di misurazione. Per gli esperimenti riportati si è scelto:

   | carico ρ          | transitorio (s) | misurazione (s) |
   |-------------------|-----------------|-----------------|
   | ρ ≥ 0.90          | 1500            | 20 000          |
   | 0.75 ≤ ρ < 0.90   | 1000            | 12 000          |
   | ρ < 0.75          |  500            |  6 000          |

   A regime di traffico pesante la varianza delle stime aumenta: si
   compensa con finestre più lunghe.
6. **Definizione di carico in scenari eterogenei.** Per i serventi
   eterogenei si è adottato `ρ = λ / Σ μ_i` come carico globale. La
   stabilità di Random/Round-robin richiede però la condizione *locale*
   `λ/N < min(μ_i)`: questa asimmetria sarà evidente nei risultati dello
   scenario B.
7. **Pre-esistenti warning sui file legacy.** Gli avvisi `-Wdeprecated`,
   `-Wwritable-strings` e `-Wunused-but-set-variable` emessi da clang sui
   file `.c` originali non modificati (`buffer.c`, `easyio.c`,
   `calendar.c`, ecc.) vengono silenziati selettivamente nel `Makefile`,
   senza modificare quei sorgenti. I file modificati per il progetto
   (`queue.{c,h}`, `event.{c,h}`, `main.c`) compilano con
   `-Wall -Wextra` senza alcun warning.


## 4. Risultati sperimentali

Tutti i numeri riportati derivano dall'esecuzione dello script `./runner`,
che esegue 7 + 5 + 4 = 16 configurazioni per ciascuna delle 3 politiche,
per un totale di 48 simulazioni. I dati grezzi sono salvati in
`scenarioA.csv`, `scenarioB.csv`, `scenarioC.csv`.

### 4.1 Scenario A — Serventi omogenei

Configurazione: `N = 4`, `μ_i = 1` ∀ i, `λ ∈ {1.0, 2.0, 2.5, 3.0, 3.2,
3.5, 3.8}` (carico globale `ρ = λ/N` da 0.25 a 0.95).

![Sojourn vs λ — Scenario A](grafici/fig1_W_vs_lambda_A.png)

![Sbilanciamento vs λ — Scenario A](grafici/fig2_imb_vs_lambda_A.png)

Tabella riassuntiva (estratto):

| λ | ρ | W Random | W RR | W JSQ | I Random | I RR | I JSQ |
|---|---|----------|------|-------|----------|------|-------|
| 1.0 | 0.25 |  1.29 |  1.08 |  1.00 |  0.04 |  0.01 |  0.02 |
| 2.0 | 0.50 |  2.13 |  1.52 |  1.19 |  0.22 |  0.04 |  0.04 |
| 3.0 | 0.75 |  3.97 |  2.62 |  1.70 |  0.34 |  0.18 |  0.01 |
| 3.2 | 0.80 |  4.65 |  3.12 |  1.91 |  0.79 |  0.20 |  0.01 |
| 3.5 | 0.88 |  8.17 |  5.25 |  2.72 |  1.12 |  1.07 |  0.02 |
| 3.8 | 0.95 | 21.80 | 13.71 |  5.01 |  8.63 |  6.16 |  0.01 |

**Osservazioni.**

- A basso carico (ρ ≤ 0.5) le tre politiche danno valori di `W`
  ravvicinati: il sistema è scarsamente saturato e l'effetto della
  politica è marginale.
- Avvicinandosi alla saturazione le politiche divergono nettamente:
  a ρ = 0.95 JSQ ottiene `W = 5.01` s contro `13.71` s di RR e `21.80`
  s di Random.
- **Validazione analitica.** Per la politica Random in regime simmetrico
  ciascun servente è una M/M/1 indipendente con tasso di arrivo `λ/N` e
  carico `ρ_local = λ/(Nμ) = ρ`. La formula M/M/1 prevede
  `W = 1/(μ(1-ρ))`, dunque:
    - ρ = 0.5 → W_teor = 2.00 s (misurato 2.13 s, +6%);
    - ρ = 0.75 → W_teor = 4.00 s (misurato 3.97 s, –0.7%);
    - ρ = 0.95 → W_teor = 20.0 s (misurato 21.8 s, +9%).
  L'accordo è eccellente, conferma la correttezza dell'implementazione.
- L'indice di sbilanciamento di JSQ resta pressoché costante e
  prossimo a zero su tutto il range di carico: JSQ bilancia
  efficacemente le code anche a saturazione. Random invece accumula
  squilibrio sempre maggiore al crescere di `λ`, perché le fluttuazioni
  Poissoniane vengono amplificate dalla coda a carico elevato.


### 4.2 Scenario B — Serventi eterogenei

Configurazione: `N = 4`, `μ = [2.0, 1.5, 1.0, 0.5]` (Σμ = 5.0),
`λ ∈ {1.0, 2.0, 3.0, 4.0, 4.5}`.

![Sojourn vs λ — Scenario B](grafici/fig3_W_vs_lambda_B.png)

![Sbilanciamento vs λ — Scenario B](grafici/fig4_imb_vs_lambda_B.png)

![Lunghezze per coda a λ=2.0 — Scenario B](grafici/fig5_bar_Ls_B.png)

Tabella riassuntiva:

| λ | ρ_globale | W Random | W RR | W JSQ | I Random | I RR | I JSQ |
|---|-----------|----------|------|-------|----------|------|-------|
| 1.0 | 0.20 |    1.66 |    1.23 |  0.95 |    0.84 |    0.53 |  0.22 |
| 2.0 | 0.40 |   15.91 |   20.34 |  0.99 |   29.47 |   39.12 |  0.33 |
| 3.0 | 0.60 |  213.22 |  204.78 |  1.15 |  850.10 |  832.23 |  0.49 |
| 4.0 | 0.80 |  492.69 |  533.63 |  1.79 | 3279.82 | 3467.31 |  0.69 |
| 4.5 | 0.90 | 1176.84 | 1222.44 |  2.78 | 7318.78 | 7050.79 |  0.86 |

**Osservazioni.**

- **Stabilità.** Random e Round-robin instradano in media `λ/N = λ/4`
  pacchetti al secondo verso ciascun servente; perché tutti i serventi
  siano stabili occorre `λ/4 < min(μ_i) = 0.5`, cioè `λ < 2.0`. Per
  `λ ≥ 2.0` il servente più lento (`μ_3 = 0.5`) è sovraccarico e la sua
  coda cresce in modo lineare nel tempo: i valori riportati di `W` non
  rappresentano un regime stazionario ma stime sulla durata finita di
  simulazione.
- **JSQ adatta il routing** a `μ_i`: al servente lento arrivano meno
  pacchetti, e il sistema resta stabile fintanto che `λ < Σμ = 5.0`. A
  `λ = 4.5` (ρ = 0.9 globale) JSQ raggiunge `W = 2.78` s contro i
  *tre ordini di grandezza* in più di Random/RR.
- **Bar chart (fig. 5).** A `λ = 2.0` si vede chiaramente come Random e
  RR concentrino il carico sul servente più lento (`L_3 ≈ 30–40` pkt)
  mentre JSQ produce lunghezze di coda *crescenti* con `1/μ_i` ma tutte
  contenute (`L_i ≤ 0.7` pkt). La scala logaritmica del grafico è
  necessaria perché lo squilibrio si estende su due ordini di grandezza.


### 4.3 Scenario C — Scalabilità

Configurazione: `N ∈ {2, 4, 8, 16}`, `μ_i = 1` ∀ i, `λ = 0.8 · N` (cioè
carico per servente fisso a `ρ = 0.8`).

![Sojourn vs N — Scenario C](grafici/fig6_W_vs_N_C.png)

Tabella:

| N | λ | W Random | W RR | W JSQ |
|---|---|----------|------|-------|
|  2 |  1.6 | 4.97 | 3.76 | 3.07 |
|  4 |  3.2 | 4.65 | 3.12 | 1.91 |
|  8 |  6.4 | 4.91 | 3.09 | 1.45 |
| 16 | 12.8 | 5.21 | 2.85 | 1.21 |

**Osservazioni.**

- **Random non scala.** Ogni servente vede un sotto-processo
  indipendente di carico ρ = 0.8; il valor medio `W` previsto da M/M/1
  è `1/(μ(1-ρ)) = 5.0` s, indipendente da `N`. I valori misurati
  oscillano intorno a 5 s, in pieno accordo.
- **Round-robin migliora poco con `N`**, perché distribuisce ancora gli
  arrivi senza guardare lo stato. Il leggero miglioramento (da 3.76 a
  2.85) è dovuto alla maggiore "regolarità" degli arrivi vista dal
  singolo servente (gli inter-arrivi locali, deterministicamente uno
  ogni `N` arrivi, hanno minor varianza).
- **JSQ trae beneficio dal multiplexing statistico.** Aumentando `N`
  l'algoritmo dispone di più code fra cui scegliere e tende a uniformare
  i tempi di attesa: `W` passa da 3.07 (N=2) a 1.21 (N=16), un
  miglioramento di 2.5× in soli 3 raddoppi di `N`.


## 5. Analisi e discussione

**Quale politica performa meglio.** In tutti i regimi simulati JSQ
domina, sia per il sojourn time sia per l'indice di sbilanciamento. La
differenza è quantitativamente irrisoria a basso carico (dove tutte e
tre le politiche si comportano simili a code M/M/1 leggermente cariche)
ma diventa drammatica man mano che ρ si avvicina a 1: a ρ = 0.95
(scenario A) `W_JSQ = 5.01` s contro `W_Random = 21.80` s, un rapporto
di 4.3×.

**Comportamento dell'indice di sbilanciamento.** L'indice è il
descrittore più discriminante delle tre politiche. JSQ mantiene
`I = max L_i − min L_i` praticamente costante e prossimo a zero in tutto
lo scenario A (≤ 0.05 anche a ρ = 0.95). Random e Round-robin hanno
invece andamento crescente: Round-robin contiene meglio le fluttuazioni
fino a carichi medi (`I = 0.18` a ρ = 0.75), ma a saturazione i due
peggiorano in modo qualitativamente simile (a ρ = 0.95 `I` = 8.6 per
Random e 6.2 per Round-robin).

**Effetto dei serventi eterogenei.** Lo scenario B mostra che Random e
Round-robin non sono adatte a sistemi con serventi eterogenei: il
servente lento determina di fatto il throughput massimo del sistema
(`λ_max = N · min(μ_i)`), molto inferiore alla capacità aggregata
`Σ μ_i`. JSQ invece sfrutta automaticamente l'intero `Σ μ_i`,
allungando il range di stabilità di un fattore `Σμ / (N·min(μ_i)) = 5/2 =
2.5` nel caso considerato. Questo riflette un risultato classico della
teoria delle code: JSQ è ottimale (in senso stocastico) per minimizzare
il sojourn time atteso, sotto ampie ipotesi.

**Accordo con M/M/1.** Le verifiche analitiche dello scenario A (Random)
e dello scenario C (Random) confermano la correttezza
dell'implementazione: l'errore relativo sulle stime di `W` rispetto alla
formula M/M/1 `W = 1/(μ(1-ρ))` è dell'ordine del ±10%, dominato dalla
varianza statistica residua a finestre di misurazione finite.

**Limiti.** Tutti i risultati sono basati su un singolo replicato per
configurazione (single run con un seme RNG fisso). Per stime con
intervalli di confidenza serve replicare le simulazioni (la classe
`Sstat` preesistente lo supporta) — fuori scope per questa relazione,
ma facilmente abilitabile.


## 6. Conclusioni

L'estensione del simulatore G/G/1 a un sistema multi-servente `N`-coda
si è ottenuta con modifiche minime alla codebase originale, riusando
calendario, eventi, code e statistiche preesistenti. Le tre politiche di
routing sono state implementate come strategia selezionabile (`enum
routing_policy`) e validate sperimentalmente su tre scenari complementari.

I risultati sperimentali sono coerenti con la teoria delle code e con
le aspettative: **JSQ è uniformemente la migliore politica** sia in
termini di tempo medio di permanenza sia di equilibrio del carico fra
le code; il suo vantaggio cresce sia con il carico sia con il numero di
serventi. Random e Round-robin sono adeguati solo a basso carico e con
serventi omogenei; in presenza di eterogeneità diventano rapidamente
inservibili, sotto-utilizzando i serventi veloci e saturando quelli
lenti.

Tutti i file modificati (`queue.{c,h}`, `event.{c,h}`, `main.c`)
compilano con `g++ -Wall -Wextra` senza warning; l'intera pipeline
(`make && ./runner`) esegue le 48 simulazioni e rigenera CSV e grafici
in meno di 3 secondi.

### File da consegnare

- `queue.h`, `queue.c` — simulatore multi-coda (file *modificati*)
- `event.h`, `event.c` — eventi `arrival` e `service` (file *modificati*)
- `main.c` — entry point con dispatch interactive/batch (file *modificato*)
- `Makefile` — build script (file *modificato*)
- `runner.cpp` — tooling ausiliario per gli esperimenti della relazione
- `relazione.md` con la cartella `grafici/` (6 PNG)
- `scenarioA.csv`, `scenarioB.csv`, `scenarioC.csv` — dati grezzi

Tutti gli altri file (`buffer.*`, `packet.*`, `calendar.*`, `stat.*`,
`simulator.*`, `rand.*`, `easyio.*`, `global.h`) sono parte della
codebase fornita e non sono stati toccati.
