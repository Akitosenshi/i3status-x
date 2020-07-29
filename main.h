#ifndef MAIN_H
#define MAIN_H

#include <pthread.h>
#include <stdio.h>

#define BUFFER_SIZE 1024 * 1024

const char* TXPATH = "/sys/class/net/%s/statistics/tx_bytes";
const char* RXPATH = "/sys/class/net/%s/statistics/rx_bytes";

const char** IFNAMES = {		//eno|enp|ens|enx|eth|wlan|wlp
	"eno",
	"enp",
	"ens",
	"enx",
	"eth",
	"wlan",
	"wlp"
};

const int IFNAMECOUNT = 7;

const int* IFNAMELEN = {
	3,
	3,
	3,
	3,
	3,
	4,
	3
};

struct threadData {
	int* readbytes;
	char* buffer;
	char* socketPath;
	pthread_mutex_t* mutex;
};

struct interfaceByteFiles{
	struct interfaceByteFiles* next;
	struct interfaceByteFiles* prev;
	FILE* rx;
	FILE* tx;
};

struct interfaceByteFiles* getIfFiles();
void freeIfList(struct interfaceByteFiles*);
void threadFunc(void*);
int prependRate(char*, FILE*, FILE*, int);

#endif
