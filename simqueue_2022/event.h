/* -*- C++ -*- */
/*******************************************************
            EVENT . H

    Eventi del simulatore. Modificato per il progetto C3:
    arrival e service ora referenziano la classe `queue`
    (sistema multi-servente) tramite puntatore, invece
    del singolo `buffer*`. service trasporta anche
    l'indice del servente che completa il servizio.
*******************************************************/

#ifndef _EVENT_H
#define _EVENT_H

#include "global.h"

// Forward declaration: event.cpp/.c include queue.h dove necessario.
class queue;

class event {
public:
    event*  next;
    double  time;
    event();
    event(double Time);
    event(event* Next, double Time);
    virtual ~event() {}
    virtual void body() {}
};

inline event::event() {
    next = NULL;
    time = -1;
}

inline event::event(event* Next, double Time) {
    next = Next;
    time = Time;
}

inline event::event(double Time) {
    next = NULL;
    time = Time;
}

// -------------------------------------------------------------
// arrival: arrivo di un pacchetto al sistema. body() schedula
// il prossimo arrivo (inter-arrivo esponenziale di media 1/lambda),
// sceglie la coda di destinazione secondo la politica configurata
// nel `queue` e, se il servente scelto era idle, schedula il
// relativo evento di servizio.
// -------------------------------------------------------------
class arrival: public event {
    queue* sys;
public:
    arrival(double Time, queue* Sys): event(Time) { sys = Sys; }
    virtual ~arrival() {}
    virtual void body();
};

// -------------------------------------------------------------
// service: completamento del servizio del servente srv_id. body()
// rimuove il pacchetto in testa, aggiorna le statistiche tempo-pesate
// e di sojourn, e se la coda non e' vuota schedula il prossimo servizio.
// -------------------------------------------------------------
class service: public event {
    queue* sys;
    int    srv_id;
public:
    service(double Time, queue* Sys, int Id): event(Time) {
        sys = Sys; srv_id = Id;
    }
    virtual ~service() {}
    virtual void body();
};

#endif
