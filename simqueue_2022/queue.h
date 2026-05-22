/* -*- C++ -*- */
/*******************************************************
            QUEUE . H

    Simulatore a eventi discreti del sistema multi-coda
    del progetto C3.

    Il file estende l'originale simulatore G/G/1 per
    supportare N serventi indipendenti, ognuno con la
    propria coda FIFO infinita, alimentati da un unico
    processo di arrivi di Poisson di tasso lambda e
    instradati secondo una politica selezionabile.
*******************************************************/
#ifndef _QUEUE_H
#define _QUEUE_H

#include <vector>
#include "simulator.h"
#include "calendar.h"
#include "event.h"
#include "buffer.h"
#include "packet.h"
#include "stat.h"

// Politiche di instradamento dei pacchetti verso le N code.
enum routing_policy {
    POL_RANDOM      = 0,   // scelta uniforme tra le N code
    POL_ROUND_ROBIN = 1,   // assegnazione ciclica deterministica
    POL_SHORTEST    = 2    // join the shortest queue (parita': random)
};

class queue: public simulator {
public:
    queue(int argc, char* argv[]);
    virtual ~queue();
    virtual void init();
    virtual void run();
    virtual void results();

    // Modalita' batch: imposta i parametri direttamente da codice
    // (senza prompt interattivi) e produce output in formato CSV.
    void set_batch(int N_, double lambda_,
                   const std::vector<double>& mu_,
                   int policy_, double trans_, double simlen_);
    void results_csv();

    // ---- stato esposto agli eventi (arrival / service) ----
    int                  N;        // numero di serventi
    double               lambda;   // tasso totale di arrivo (Poisson)
    std::vector<double>  mu;       // tassi di servizio per servente
    routing_policy       policy;   // politica di instradamento

    std::vector<buffer*> queues;   // code FIFO indipendenti
    std::vector<int>     busy;     // 0 = idle, 1 = servente occupato
    std::vector<int>     lenq;     // pacchetti nel sistema-servente

    // Accumulatori tempo-pesati per la lunghezza media di ogni coda.
    std::vector<double>  last_t;
    std::vector<double>  area;

    // Accumulatori globali per il tempo medio di permanenza.
    double               sojourn_sum;
    long                 pkt_done;

    int                  rr_next;       // contatore round-robin
    bool                 in_run_phase;  // true oltre il transitorio

    // Routing e schedulazione (chiamati dagli eventi).
    int  route_packet();
    void update_area(int i, double now);
    void schedule_service(int i, double now);
    void schedule_arrival(double now);

protected:
    virtual void input();
    virtual void print_trace(int Run);

private:
    double trans_len;    // durata del transitorio (s)
    double sim_len;      // durata della fase di misurazione (s)
    double stats_start;  // istante in cui partono le statistiche
    double clock_now;    // istante simulato al termine della run
    bool   batch_mode;   // se true init() salta il prompt input()

    const char* policy_name() const;
};

#endif
