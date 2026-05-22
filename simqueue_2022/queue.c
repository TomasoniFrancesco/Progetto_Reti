/* -*- C++ -*- */
/*******************************************************
            QUEUE . C

    Implementazione del simulatore multi-coda (progetto C3).
*******************************************************/

#include <cstdio>
#include <climits>
#include "global.h"
#include "queue.h"
#include "rand.h"
#include "easyio.h"

// Calendario eventi globale condiviso fra arrivi e servizi.
// Riferito da event.c tramite "extern calendar* cal".
calendar* cal = NULL;

// =================================================================
// Costruttore / distruttore
// =================================================================
queue::queue(int argc, char* argv[]): simulator(argc, argv) {
    cal          = new calendar();
    N            = 0;
    lambda       = 0.0;
    policy       = POL_RANDOM;
    sojourn_sum  = 0.0;
    pkt_done     = 0;
    rr_next      = 0;
    in_run_phase = false;
    trans_len    = 0.0;
    sim_len      = 0.0;
    stats_start  = 0.0;
    clock_now    = 0.0;
    batch_mode   = false;
}

queue::~queue() {
    for (int i = 0; i < N; ++i) {
        delete queues[(size_t)i];
    }
    delete cal;
    cal = NULL;
}

// =================================================================
// Impostazione parametri in modalita' batch
// =================================================================
void queue::set_batch(int N_, double lambda_,
                      const std::vector<double>& mu_,
                      int policy_, double trans_, double simlen_) {
    N         = N_;
    lambda    = lambda_;
    mu        = mu_;
    policy    = (routing_policy)policy_;
    trans_len = trans_;
    sim_len   = simlen_;
    batch_mode = true;
}

// =================================================================
// Input interattivo (modalita' default).
// =================================================================
void queue::input() {
    fprintf(fpout, "\nPARAMETRI DEL MODELLO\n");
    // Assunzione: N>=1, default 2 serventi.
    N = read_int((char*)"Numero di serventi N", 2, 1, 100);

    // Tasso totale di arrivi al sistema (Poisson).
    lambda = read_double((char*)"Tasso di arrivo lambda (pkt/s)",
                         1.0, 0.0001, 1.0e6);

    mu.resize((size_t)N);
    for (int i = 0; i < N; ++i) {
        char prompt[80];
        std::snprintf(prompt, sizeof(prompt),
                      "Tasso di servizio mu[%d] (pkt/s)", i);
        // Assunzione: ogni mu_i puo' essere diverso. Default mu=1.
        mu[(size_t)i] = read_double(prompt, 1.0, 0.0001, 1.0e6);
    }

    fprintf(fpout, "\nPolitica di instradamento:\n");
    fprintf(fpout, "  0 - Random (uniforme tra le N code)\n");
    fprintf(fpout, "  1 - Round-robin (ciclico deterministico)\n");
    fprintf(fpout, "  2 - Shortest queue (JSQ, parita' a caso)\n");
    int p = read_int((char*)"Scelta", 0, 0, 2);
    policy = (routing_policy)p;

    fprintf(fpout, "\nPARAMETRI DI SIMULAZIONE\n");
    // Assunzione: il transitorio (warm-up) si scarta; le statistiche
    // partono al termine del transitorio.
    trans_len = read_double((char*)"Durata del transitorio (s)",
                            100.0, 0.0, 1.0e9);
    sim_len   = read_double((char*)"Durata fase di misurazione (s)",
                            1000.0, 0.01, 1.0e9);
}

// =================================================================
// Inizializzazione: alloca code/contatori e schedula il primo arrivo.
// =================================================================
void queue::init() {
    if (!batch_mode) {
        input();
    }

    queues.assign((size_t)N, (buffer*)NULL);
    busy  .assign((size_t)N, 0);
    lenq  .assign((size_t)N, 0);
    last_t.assign((size_t)N, 0.0);
    area  .assign((size_t)N, 0.0);
    for (int i = 0; i < N; ++i) {
        queues[(size_t)i] = new buffer();
    }

    // Primo arrivo a t=0; gli inter-arrivi successivi sono esponenziali.
    cal->put(new arrival(0.0, this));
}

// =================================================================
// Helper: schedulazione di eventi.
// =================================================================
void queue::schedule_arrival(double now) {
    double iat;
    GEN_EXP(SEED, 1.0 / lambda, iat);
    cal->put(new arrival(now + iat, this));
}

void queue::schedule_service(int i, double now) {
    double svc;
    GEN_EXP(SEED, 1.0 / mu[(size_t)i], svc);
    cal->put(new service(now + svc, this, i));
}

// Aggiorna l'integrale tempo-pesato area += len(t) * dt.
void queue::update_area(int i, double now) {
    size_t k = (size_t)i;
    area[k]  += (double)lenq[k] * (now - last_t[k]);
    last_t[k] = now;
}

// =================================================================
// Politica di instradamento del pacchetto in arrivo.
// =================================================================
int queue::route_packet() {
    if (policy == POL_RANDOM) {
        int idx;
        GEN_UNIF(SEED, 0, N - 1, idx);
        return idx;
    }
    if (policy == POL_ROUND_ROBIN) {
        int idx = rr_next;
        rr_next = (rr_next + 1) % N;
        return idx;
    }
    // POL_SHORTEST: cerca il minimo di lenq[]; in caso di parita',
    // scegli a caso fra gli indici a pari lunghezza minima.
    int min_len = INT_MAX;
    for (int i = 0; i < N; ++i) {
        if (lenq[(size_t)i] < min_len) min_len = lenq[(size_t)i];
    }
    std::vector<int> tied;
    tied.reserve((size_t)N);
    for (int i = 0; i < N; ++i) {
        if (lenq[(size_t)i] == min_len) tied.push_back(i);
    }
    int t_idx;
    GEN_UNIF(SEED, 0, (int)tied.size() - 1, t_idx);
    return tied[(size_t)t_idx];
}

// =================================================================
// Run: transitorio + fase di misurazione.
// =================================================================
void queue::run() {
    event* ev;
    double t = 0.0;

    // --- Fase 1: transitorio (gli accumulatori si azzerano al termine) ---
    in_run_phase = false;
    while (t < trans_len) {
        ev = cal->get();
        if (ev == NULL) break;
        ev->body();
        t = ev->time;
        delete ev;
    }
    // Reset degli accumulatori: la fase di misurazione parte da t corrente.
    stats_start = t;
    for (int i = 0; i < N; ++i) {
        last_t[(size_t)i] = t;
        area  [(size_t)i] = 0.0;
    }
    sojourn_sum = 0.0;
    pkt_done    = 0;
    in_run_phase = true;

    // --- Fase 2: misurazione ---
    double t_end = stats_start + sim_len;
    while (t < t_end) {
        ev = cal->get();
        if (ev == NULL) break;
        ev->body();
        t = ev->time;
        delete ev;
    }

    // Chiude l'integrale dell'area fino all'istante finale.
    for (int i = 0; i < N; ++i) {
        area[(size_t)i] += (double)lenq[(size_t)i]
                           * (t - last_t[(size_t)i]);
        last_t[(size_t)i] = t;
    }
    clock_now = t;
}

// =================================================================
// Output dei risultati (umani e CSV).
// =================================================================
const char* queue::policy_name() const {
    switch (policy) {
        case POL_RANDOM:      return "Random (uniforme)";
        case POL_ROUND_ROBIN: return "Round-robin";
        case POL_SHORTEST:    return "Shortest queue (JSQ)";
    }
    return "?";
}

void queue::results() {
    double T = clock_now - stats_start;
    double mean_sojourn = (pkt_done > 0)
        ? sojourn_sum / (double)pkt_done
        : 0.0;

    fprintf(fpout, "\n*****************************************************\n");
    fprintf(fpout, "         RISULTATI DELLA SIMULAZIONE (progetto C3)\n");
    fprintf(fpout, "*****************************************************\n\n");
    fprintf(fpout, "Parametri di input:\n");
    fprintf(fpout, "  Politica di instradamento  : %s\n", policy_name());
    fprintf(fpout, "  Numero di serventi N       : %d\n", N);
    fprintf(fpout, "  Tasso di arrivo lambda     : %.4f pkt/s\n", lambda);
    for (int i = 0; i < N; ++i) {
        fprintf(fpout, "    mu[%d]                    : %.4f pkt/s\n",
                i, mu[(size_t)i]);
    }
    fprintf(fpout, "  Transitorio (scartato)     : %.4f s\n", trans_len);
    fprintf(fpout, "  Fase di misurazione        : %.4f s\n", T);
    fprintf(fpout, "  Pacchetti serviti misurati : %ld\n\n", pkt_done);

    fprintf(fpout, "Metriche:\n");
    fprintf(fpout, "  Tempo medio di permanenza nel sistema (sojourn): "
                   "%.6f s\n\n", mean_sojourn);

    fprintf(fpout, "  Lunghezza media di ogni coda\n");
    fprintf(fpout, "  (pacchetti totali nel sistema-servente, ovvero in "
                   "coda + in servizio):\n");
    double Lmin =  1.0e300;
    double Lmax = -1.0e300;
    for (int i = 0; i < N; ++i) {
        double Li = (T > 0.0) ? area[(size_t)i] / T : 0.0;
        if (Li < Lmin) Lmin = Li;
        if (Li > Lmax) Lmax = Li;
        fprintf(fpout, "    L[%d] = %.6f pkt\n", i, Li);
    }
    fprintf(fpout, "\n  Indice di sbilanciamento (L_max - L_min): %.6f pkt\n",
            Lmax - Lmin);
    fprintf(fpout, "*****************************************************\n");
}

// Stampa una singola riga CSV con i risultati. Usata dal runner.
// Schema:
//   CSV,policy,N,lambda,trans,sim_T,W,imbalance,pkts,L0,L1,...
void queue::results_csv() {
    double T = clock_now - stats_start;
    double mean_sojourn = (pkt_done > 0)
        ? sojourn_sum / (double)pkt_done
        : 0.0;
    double Lmin =  1.0e300;
    double Lmax = -1.0e300;
    std::vector<double> Ls((size_t)N, 0.0);
    for (int i = 0; i < N; ++i) {
        Ls[(size_t)i] = (T > 0.0) ? area[(size_t)i] / T : 0.0;
        if (Ls[(size_t)i] < Lmin) Lmin = Ls[(size_t)i];
        if (Ls[(size_t)i] > Lmax) Lmax = Ls[(size_t)i];
    }
    fprintf(fpout, "CSV,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%ld",
            (int)policy, N, lambda, trans_len, T, mean_sojourn,
            Lmax - Lmin, pkt_done);
    for (int i = 0; i < N; ++i) {
        fprintf(fpout, ",%.6f", Ls[(size_t)i]);
    }
    fprintf(fpout, "\n");
}

// In questa versione la simulazione e' single-run: niente tracce per-run.
void queue::print_trace(int /*Run*/) {
}
