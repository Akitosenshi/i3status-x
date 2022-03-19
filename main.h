#ifndef MAIN_H
#define MAIN_H

#include <pthread.h>
#include <stdio.h>

#define BUFFER_SIZE 1024 * 2556

struct thread_data {
	int* readbytes;
	volatile char* buffer;
	char* socket_path;
	pthread_mutex_t* mutex;
};

int main(int, char**);
void threadFunc(void*);
int prependRate(char*, int, pthread_mutex_t*);

#endif
