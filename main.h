#ifndef MAIN_H
#define MAIN_H

#include <pthread.h>
#include <stdio.h>

#define BUFFER_SIZE 1024 * 1024

struct threadData {
	int* readbytes;
	char* buffer;
	char* socketPath;
	pthread_mutex_t* mutex;
};

void threadFunc(void*);
int prependRate(char*, FILE*, FILE*, int);

#endif
