/*******************************************************
		     EVENT . H
*******************************************************/


#ifndef _EVENT_H
#define _EVENT_H

#include "global.h"
#include "buffer.h"


class event{
public:
	event*	next;	// next event
	double 	time;	// event time
	event();
	event(double Time);
	event(event* Next, double Time);
	~event(){}
	virtual void body(){}
};

inline event::event(){
	next=NULL;
	time=-1;
	}

inline event::event(event* Next, double Time){
	next=Next;
	time=Time;
	}

inline event::event(double Time){
	time=Time;
	}

class arrival: public event{

	buffer** bufs;		// CAMBIATO: array di tutti gli N buffer (uno per server), serve per instradare

	public:
	int source_id;
	virtual void body();
	arrival(double Time, buffer** Bufs);	// CAMBIATO: riceve l'array di buffer
	};

class service: public event{

	buffer* buf;
	int	server_id;	// NUOVO: indice del server a cui appartiene questo servizio (per scegliere il giusto mu_i)

	public:
	virtual void body();
	service(double Time, buffer* Buf, int Server_id): event(Time){buf=Buf; server_id=Server_id;}	// CAMBIATO: memorizza anche il server_id
	};

inline arrival::arrival(double Time, buffer** Bufs): event(Time){
	bufs=Bufs;		// CAMBIATO: salva l'array di buffer
	}

#endif

