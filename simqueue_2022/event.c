/* -*- C++ -*- */
/***********************************************************************
        EVENT . C

    Implementazione dei body() per i due eventi del simulatore
    multi-servente (progetto C3).
***********************************************************************/

#include "event.h"
#include "queue.h"
#include "buffer.h"
#include "calendar.h"
#include "packet.h"
#include "rand.h"

// Calendario eventi globale e condiviso fra tutti i serventi.
// Definito (e allocato) in queue.c.
extern calendar* cal;

// -------------------------------------------------------------
// arrival::body()
// -------------------------------------------------------------
// 1. Genera (e mette in calendario) il prossimo arrivo al sistema.
// 2. Sceglie la coda di destinazione con la politica configurata.
// 3. Aggiorna l'integrale tempo-pesato della coda scelta.
// 4. Inserisce il nuovo pacchetto nella coda di destinazione.
// 5. Se quel servente era idle, schedula immediatamente il servizio.
void arrival::body() {
    // Prossimo arrivo: inter-arrivo esponenziale con media 1/lambda.
    double iat;
    GEN_EXP(SEED, 1.0 / sys->lambda, iat);
    cal->put(new arrival(time + iat, sys));

    // Instradamento del pacchetto corrente verso una delle N code.
    int i = sys->route_packet();

    // Aggiorna l'integrale lenq(t) dt PRIMA di modificare lenq[i].
    sys->update_area(i, time);

    // Inserisce il pacchetto in coda (memorizza l'istante di arrivo
    // per il successivo calcolo del sojourn time).
    sys->queues[i]->insert(new packet(time));
    sys->lenq[i] += 1;

    // Se il servente era idle, comincia subito a servire questo pacchetto.
    if (!sys->busy[i]) {
        sys->busy[i] = 1;
        sys->schedule_service(i, time);
    }
}

// -------------------------------------------------------------
// service::body()
// -------------------------------------------------------------
// 1. Estrae il pacchetto in testa alla coda del servente srv_id.
// 2. Aggiorna l'integrale tempo-pesato.
// 3. Aggiorna le statistiche di sojourn (solo fuori dal transitorio).
// 4. Se la coda non e' vuota, schedula il servizio del successivo;
//    altrimenti il servente diventa idle.
void service::body() {
    packet* p = sys->queues[srv_id]->get();

    // Aggiorna area PRIMA di decrementare lenq.
    sys->update_area(srv_id, time);
    sys->lenq[srv_id] -= 1;

    if (p != NULL) {
        if (sys->in_run_phase) {
            sys->sojourn_sum += time - p->get_time();
            sys->pkt_done    += 1;
        }
        delete p;
    }

    if (sys->lenq[srv_id] > 0) {
        sys->schedule_service(srv_id, time);
    } else {
        sys->busy[srv_id] = 0;
    }
}
