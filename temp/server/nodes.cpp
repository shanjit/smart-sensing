#include "nodes.h"
#include "packet.h"

#include <cstring>
#include <map>
#include <list>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

Server_Main Server;

int extract_key(struct sockaddr_in addr){

	char *ip = inet_ntoa(addr.sin_addr);
	int port = ntohs(addr.sin_port);
	
	char *k;
	k = strtok(ip,".");
	for(int i=0;i<3;++i)
		k = strtok(NULL,".");
	return (atoi(k)+port);
}

Actuator::Actuator(struct sockaddr_in addr){
	
	address = addr;
	status = false;
	key = extract_key(address);

	//setting up the timer
	sigemptyset(&timeup.sa_mask);
	timeup.sa_handler = TimerHandler;
	// set up sigaction to catch signal
	if (sigaction(key, &timeup, NULL) == -1) {
		perror("sigaction failed");
		exit(EXIT_FAILURE);
	}
	Server.A_keys.push_back(key);
	Server.A_map[key]=*this;
}

void Actuator::set_status(int time_out){
	status = true;
	tid = SetTimer(time_out);
	event_time = time(NULL);
}

void Actuator::reset(){
	status = false;
}

bool Actuator::get_status(){
	return status;
}

double Actuator::get_time(){
	if (status)
		return (difftime(time(NULL), event_time));
	return 0;
}

struct sockaddr_in Actuator::get_addr(){
	return address;
}

void Actuator::TimerHandler(int signo){
	Server.A_map[signo].reset();
}

timer_t Actuator::SetTimer(int time_out){

	static struct sigevent sigev;
	static timer_t tid;
	static struct itimerspec itval;
	static struct itimerspec oitval;
	int signo = key;
	// Create the POSIX timer to generate signo
	sigev.sigev_notify = SIGEV_SIGNAL;
	sigev.sigev_signo = signo;
	sigev.sigev_value.sival_ptr = &tid;

	if (timer_create(CLOCK_REALTIME, &sigev, &tid) == 0) {
		itval.it_value.tv_sec = time_out;
		itval.it_value.tv_nsec = 0;

		itval.it_interval.tv_sec = 0;
		itval.it_interval.tv_nsec = 0;

		if (timer_settime(tid, 0, &itval, &oitval) != 0) {
			perror("time_settime error!");
		}
	}
	else {
		perror("timer_create error!");
		return NULL;
	}
	return tid;
}


Sensor::Sensor(struct sockaddr_in addr){
	address = addr;
	key = extract_key(address);

	Server.S_keys.push_back(key);
	Server.S_map[key] = *this;
}

void Sensor::set_self(){

	//if its actuator is already set by a neighbour node just increase the time_out to 5
	//else set the neighbours as this is the node through which intersection is entered

	if(Server.A_map[act_key].get_status()){
		if (Server.A_map[act_key].get_time()<=60)
			Server.A_map[act_key].set_status(300);
		else set_N(60);
	}
	else set_N(60);
}

void Sensor::set_N(int time_out){
	if (N_keys.empty())
		return;
	list<int>::iterator i;
	for (i = N_keys.begin(); i != N_keys.end(); ++i){
		Server.A_map[Server.S_map[*i].get_act()].set_status(time_out);
	}
}

int Sensor::get_key(){
	return key;
}

int Sensor::get_act(){
	return act_key;
}

struct sockaddr_in Sensor::get_addr(){
	return address;
}

void Sensor::add_N(int n_key){
	N_keys.push_back(n_key);
}

void Sensor::add_act(int a_key){
	act_key = a_key;
}


void Server_Main::set_localize(){
	

	map<int,Sensor>::iterator i;
	map<int,Sensor>::iterator j;
	map<int,Actuator>::iterator k;

	frame f;
	struct sockaddr_in addr_t;
	struct sockaddr_in addr_r;

	for(i = S_map.begin();i!=S_map.end();++i)
	{
		//addr_t specifies sensor node to be in transmitter mode
		addr_t = (*i).get_addr();
		for(j = S_map.begin();j!=S_map.end();++j)
		{
			if(i->first!=j->first)
			{
				f.msg_set(1,2,j->first,addr_t);
				send_queue.push_back(f);
				//send frame to send queue with addr_t as address

				//addr_r specifies sensor node in receiver mode
				addr_r = (*j).get_addr();
				f.msg_set(1,3,i->first,addr_r);
				send_queue.push_back(f);
				//send frame to send queue with addr_r as address
			}
		}
		for(k = A_map.begin();k!=A_map.end();++k)
		{
			f.msg_set(2,2,j->first,addr_t);
			send_queue.push_back(f);
			//send frame to send queue with addr_t as address
			
			//addr_r specifies actuator node in receiver mode
			addr_r = (*j).get_addr();
			f.msg_set(2,3,i->first,addr_r);
			send_queue.push_back(f);
			//send frame to send queue with addr_r as address
		}

		// set a unique lock with conditional variable
		// lock until conditional variable becomes true
		// to get a message from addr_t that RF to all the nodes have been sent
		// conditional variable needs to be set in receive queue: if mesg type 2 is received proceed
	}
}

//[1,4,N] S_map[extract_key(sockaddr_in)].add_N(N);
//[2,4,N] S_map[N].add_act(extract_key(sockaddr_in));
