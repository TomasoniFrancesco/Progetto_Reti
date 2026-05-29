/***********************************************************************
		EVENT.C
***********************************************************************/

#include "event.h"
#include "buffer.h"
#include "calendar.h"
#include "rand.h"

extern calendar* cal;
extern double inter;
extern double* duration_i;	// CAMBIATO: ora c'e' una durata media di servizio per ogni server (1/mu_i)
extern int N;			// NUOVO: numero di server
extern int routing_policy;	// NUOVO: 0=casuale, 1=round-robin, 2=coda piu' corta
extern int rr_counter;		// NUOVO: contatore usato dalla politica round-robin

void arrival::body(){
        event* ev;

	// generation of next arrival
	double esito;
	GEN_EXP(SEED, inter, esito);
	ev=new arrival(time+esito, bufs);	// CAMBIATO: il prossimo arrivo riceve l'array di buffer
	cal->put(ev);

	// NUOVO: scelta del server di destinazione secondo la politica di instradamento
	int target;
	if(routing_policy==0){
		// instradamento casuale: scegli un server a caso tra 0 e N-1
		GEN_UNIF(SEED, 0, N-1, target);
		}
	else if(routing_policy==1){
		// round-robin: assegna ciclicamente al server successivo
		target=rr_counter;
		rr_counter=(rr_counter+1)%N;
		}
	else	{
		// coda piu' corta: scegli il server con meno pacchetti in attesa
		target=0;
		for(int i=1;i<N;i++)
			if(bufs[i]->queue_size < bufs[target]->queue_size)
				target=i;
		}
	buffer* buf=bufs[target];		// NUOVO: d'ora in poi si lavora sul buffer scelto

	// insert the new packet in the queue
      packet* pack=new packet(time);
	// if some packet is already in the buffer, just insert the new one
      if(buf->full()||buf->status){
		buf->insert(pack);
		}
	// otherwise let the packet get in the service
	else	{
		buf->tot_packs+=1.0;
		GEN_EXP(SEED, duration_i[target], esito);	// CAMBIATO: tempo di servizio del server scelto
		buf->tot_delay+=esito;	// NUOVO: il pacchetto servito subito ha permanenza = tempo di servizio (correzione del sojourn)
		delete pack;
		ev=new service(time+esito, buf, target);	// CAMBIATO: passo anche il server_id
		cal->put(ev);
		buf->status=1;
		}
        }

void service::body(){
	// printf("ingresso             %f\n", time);
	packet* pack;
	pack=buf->get();
	event* ev;
	double esito;
	GEN_EXP(SEED, duration_i[server_id], esito);	// CAMBIATO: tempo di servizio specifico di questo server
	if(pack!=NULL){
		ev=new service(time+esito, buf, server_id);	// CAMBIATO: il prossimo servizio resta sullo stesso server
		cal->put(ev);
		buf->tot_delay+=time-pack->get_time()+esito;	// CAMBIATO: permanenza = attesa in coda (time-arrivo) + tempo di servizio (esito)
		// printf("%3.5f    %3.5f\n", time, time-pack->get_time());
		buf->tot_packs+=1.0;
		delete pack;
		}
	else
		buf->status=0;
	}

