#ifndef MAIN_H
#define MAIN_H

#include <pthread.h>
#include <stdio.h>

#define BUFFER_SIZE 1024 * 1024

struct threadData {
	int* readbytes;
	volatile char* buffer;
	char* socketPath;
	pthread_mutex_t* mutex;
};

int main(int, char**);
void threadFunc(void*);
int prependRate(char*, int, pthread_mutex_t*);

#endif
