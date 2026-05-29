/* -*- C++ -*- */
/*******************************************************
		QUEUE.C
*******************************************************/
#include "global.h"
#include <stdio.h>
#include "queue.h"
#include "rand.h"
#include "buffer.h"
#include "event.h"
#include "calendar.h"
#include "easyio.h"

calendar* cal;		// events calendar
double	inter;
double*	duration_i;	// CAMBIATO: durata media di servizio per ogni server (1/mu_i)
int	N;		// NUOVO: numero di server
int	routing_policy;	// NUOVO: 0=casuale, 1=round-robin, 2=coda piu' corta
int	rr_counter;	// NUOVO: contatore per il round-robin
double 	Trslen;
double 	Runlen;
int 	NRUNmin;
int 	NRUNmax;


queue::queue(int argc,char *argv[]): simulator(argc,argv)
{
cal=new calendar();
delay=new Sstat();
imbalance=new Sstat();	// NUOVO: statistica dello sbilanciamento
// NUOVO: le strutture che dipendono da N sono allocate in input(), quando N e' noto
bufs=NULL;
q_len=NULL;
duration_i=NULL;
}

queue::~queue()
{
delete delay;
delete imbalance;		// NUOVO
delete cal;
// NUOVO: libero le N code e le N statistiche di lunghezza
for(int i=0;i<N;i++){
	delete bufs[i];
	delete q_len[i];
	}
delete[] bufs;
delete[] q_len;
delete[] duration_i;
}

void queue::input(){
printf("MODEL PARAMETERS:\n\n");
	N=read_int("Number of servers (N)",2,1,100);	// NUOVO: numero di server/code
	traffic_model=1;	// CAMBIATO: la consegna fissa arrivi di Poisson, niente piu' menu' a una sola voce
	lambda=read_double("Arrival rate lambda (pkt/s)",1.0,0.001,10000);	// CAMBIATO: leggo lambda invece del carico
	inter=1.0/lambda;	// CAMBIATO: tempo medio tra arrivi = 1/lambda
	service_model=1;	// CAMBIATO: la consegna fissa servizio esponenziale, niente piu' menu' a una sola voce

	// NUOVO: ora che conosco N, alloco le strutture per server
	duration_i=new double[N];
	bufs=new buffer*[N];
	q_len=new Sstat*[N];
	// NUOVO: leggo il tasso di servizio mu_i di ogni server e creo la sua coda e la sua statistica
	for(int i=0;i<N;i++){
		char prompt[80];
		sprintf(prompt,"Service rate mu for server %d (pkt/s)",i);
		double mu=read_double(prompt,1.0,0.001,10000);
		duration_i[i]=1.0/mu;	// durata media di servizio = 1/mu_i
		bufs[i]=new buffer();	// una coda FIFO per ogni server
		q_len[i]=new Sstat();	// una statistica di lunghezza coda per ogni server
		}

	// NUOVO: scelta della politica di instradamento
	printf("\n Routing policy:\n");
	printf("0 - Random\n");
	printf("1 - Round-robin\n");
	printf("2 - Shortest queue\n");
	routing_policy=read_int("",0,0,2);
	rr_counter=0;		// NUOVO: inizializzo il contatore round-robin

printf("SIMULATION PARAMETERS:\n\n");
	Trslen=read_double("Simulation transient len (s)", 100, 0.01, 10000);
	Runlen=read_double("Simulation RUN len (s)",  100, 0.01, 10000);
	NRUNmin=read_int("Simulation number of RUNs", 5, 2, 100);
}



void queue::init()
{
input();
event* Ev;
Ev=new arrival(0.0, bufs);	// CAMBIATO: il primo arrivo riceve l'array di tutte le code
cal->put(Ev);
}

void queue::run(){
	
	extern double 	Trslen;
	extern double 	Runlen;
	extern int 	NRUNmin;
	extern int 	NRUNmax;

        double clock=0.0;
        event* ev;
        while (clock<Trslen){
        	ev=cal->get();
        	ev->body();
        	clock=ev->time; 
        	delete(ev);     
        	}
	clear_stats();
	clear_counters();
	int current_run_number=1;
	while(current_run_number<=NRUNmin){
		while (clock<(current_run_number*Runlen+Trslen)){
			ev=cal->get();
	                ev->body();
       	         	clock=ev->time;
                	delete(ev);
			}
		update_stats();
		clear_counters();
		print_trace(current_run_number);
		current_run_number++;
		}
	}


void queue::results()
{
	extern double 	Trslen;
	extern double 	Runlen;
	extern int 	NRUNmin;
	extern int 	NRUNmax;

	fprintf(fpout, "*********************************************\n");
	fprintf(fpout, "           SIMULATION RESULTS                \n");
	fprintf(fpout, "*********************************************\n\n");
	fprintf(fpout, "Input parameters:\n");
	fprintf(fpout, "Number of servers            %5d\n", N);			// NUOVO
	fprintf(fpout, "Arrival rate lambda          %5.3f\n", lambda);		// CAMBIATO
	const char* polname[3]={"Random","Round-robin","Shortest queue"};	// NUOVO: nomi delle politiche
	fprintf(fpout, "Routing policy               %s\n", polname[routing_policy]);	// NUOVO
	for(int i=0;i<N;i++)							// NUOVO: stampo il mu di ogni server
		fprintf(fpout, "  Server %2d service rate mu  %5.3f\n", i, 1.0/duration_i[i]);
	fprintf(fpout, "Transient length (s)         %5.3f\n", Trslen);
	fprintf(fpout, "Run length (s)               %5.3f\n", Runlen);
	fprintf(fpout, "Number of runs               %5d\n", NRUNmin);
	fprintf(fpout, "\nResults:\n");
	// CAMBIATO: ora e' il tempo medio di permanenza nel sistema
	fprintf(fpout, "Mean time in system          %2.6f   +/- %.2e  p:%3.2f\n",
			delay->mean(),
			delay->confidence(.95),
			delay->confpercerr(.95));
	for(int i=0;i<N;i++)							// NUOVO: lunghezza media di ogni coda
		fprintf(fpout, "Mean queue length server %2d  %2.6f   +/- %.2e\n",
				i, q_len[i]->mean(), q_len[i]->confidence(.95));
	fprintf(fpout, "Load imbalance (max-min)     %2.6f   +/- %.2e\n",	// NUOVO: indice di sbilanciamento
			imbalance->mean(), imbalance->confidence(.95));
}

void queue::print_trace(int n)
{
      fprintf(fptrc, "*********************************************\n");
      fprintf(fptrc, "                 TRACE RUN %d                \n", n);
      fprintf(fptrc, "*********************************************\n\n");

	
      fprintf(fptrc, "Mean time in system          %2.6f   +/- %.2e  p:%3.2f\n",	// CAMBIATO
                        delay->mean(),
                        delay->confidence(.95),
                        delay->confpercerr(.95));
      for(int i=0;i<N;i++)						// NUOVO: lunghezza media coda per server
		fprintf(fptrc, "Mean queue length server %2d  %2.6f\n", i, q_len[i]->mean());
      fprintf(fptrc, "Load imbalance               %2.6f\n", imbalance->mean());	// NUOVO
      fflush(fptrc);

}

void queue::clear_counters()
{
	// CAMBIATO: azzero i contatori di ogni server
	for(int i=0;i<N;i++){
		bufs[i]->tot_delay=0.0;
		bufs[i]->tot_packs=0.0;
		}
}

void queue::clear_stats()
{
	delay->reset();
	imbalance->reset();		// NUOVO
	for(int i=0;i<N;i++)		// NUOVO: azzero le statistiche di lunghezza coda
		q_len[i]->reset();
}
void queue::update_stats()
{
	extern double Runlen;

	// CAMBIATO: tempo medio di permanenza nel sistema su tutti i pacchetti di questo run
	double tot_d=0.0, tot_p=0.0;
	for(int i=0;i<N;i++){
		tot_d+=bufs[i]->tot_delay;
		tot_p+=bufs[i]->tot_packs;
		}
	if(tot_p>0) *delay+=tot_d/tot_p;

	// NUOVO: lunghezza media di ogni coda con la Legge di Little.
	// Lq_i = lambda_i * Wq_i = (somma sojourn_i - n_pacchetti_i * servizio_medio_i) / durata_run
	double maxL=-1e99, minL=1e99;
	for(int i=0;i<N;i++){
		double L=(bufs[i]->tot_delay - bufs[i]->tot_packs*duration_i[i])/Runlen;
		if(L<0) L=0;		// piccoli valori negativi dovuti al rumore statistico -> 0
		*q_len[i]+=L;
		if(L>maxL) maxL=L;	// traccio il massimo per lo sbilanciamento
		if(L<minL) minL=L;	// traccio il minimo per lo sbilanciamento
		}
	// NUOVO: indice di sbilanciamento del carico = differenza tra coda piu' lunga e piu' corta
	*imbalance+=(maxL-minL);
}

int queue::isconfsatisf(double perc)
{
        return delay->isconfsatisfied(10, .95);
}
