/***************************************************************************
			BUFFER.C
***************************************************************************/

#include "buffer.h"

buffer::buffer(){
	head=NULL;
	last=NULL;
	status=0;
	queue_size=0;		// NUOVO: la coda parte vuota
	tot_delay=0.0;
	tot_packs=0.0;
	}

void 	buffer::insert(packet* pack){
	queue_size++;		// NUOVO: un pacchetto in piu' entra in attesa
	if(head==NULL){
		head=pack;
		last=pack;
		last->next=head;
		}
	else	{
		last->next=pack;
		last=pack;
		last->next=head;
		}
	}

packet* buffer::get(){

	packet* pack;
	if(head==NULL)
		return NULL;
	queue_size--;		// NUOVO: un pacchetto esce dall'attesa (va in servizio)
	if(last==head){
		pack=head;
		last=NULL;
		head=NULL;
		}
	else	{
		pack=head;
		head=head->next;
		last->next=head;
		}
	return pack;
	}
	
