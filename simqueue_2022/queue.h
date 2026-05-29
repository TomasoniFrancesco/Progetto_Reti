/*******************************************************
		     G/G/1 QUEUE SIMULATOR
*******************************************************/
#ifndef _QUEUE_H
#define _QUEUE_H

#include "simulator.h"
#include "calendar.h"
#include "event.h"
#include "buffer.h"
#include "packet.h"
#include "stat.h"

class queue: public simulator{

	virtual void input(void);
	buffer** bufs;          // CAMBIATO: array di N buffer (una coda FIFO per ogni server)
	int	traffic_model;
	double	lambda;         // CAMBIATO: tasso di arrivo (al posto del carico "load")
	int	service_model;
	// statistics
	Sstat*	delay;          // tempo medio di permanenza nel sistema (sojourn)
	Sstat**	q_len;          // NUOVO: lunghezza media della coda, una statistica per server
	Sstat*	imbalance;      // NUOVO: sbilanciamento del carico = max - min lunghezza media di coda
public:
	queue(int argc,char *argv[]);
	virtual ~queue(void);
	virtual void init(void);
	virtual void run(void);
private:
	virtual void clear_counters(void);
	virtual void clear_stats(void);
	virtual void update_stats(void);
	virtual void print_trace(int Run);
	virtual void results(void);
	virtual int isconfsatisf(double perc);
};
#endif

