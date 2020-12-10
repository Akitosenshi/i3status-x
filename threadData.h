#ifndef THREADDATA_H
#define THREADDATA_H

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "error.h"

struct threadData {
	int* readbytes;
	volatile char* buffer;
	char* socketPath;
	pthread_mutex_t* mutex;
};

struct threadData* genThreadData(pthread_mutex_t* mutex, int* readbytes, volatile char* buffer, char *socketPath, int socketPathLength){
	threadData* td = (threadData*)malloc(sizeof(threadData*));
	if(td == NULL){
		PRINT_ERROR("error in malloc()");
		return NULL;
	}
	td->mutex = mutex;
	td->readbytes = readbytes;
	td->buffer = buffer;
	td->socketPath = (char*)malloc(strlen(socketPath) + 1);
	if(td->socketPath == NULL) {
			PRINT_ERROR("error in malloc()");
			free(td);
			return NULL;
	}
	memcpy(td->socketPath, socketPath, socketPathLength);
	return td;
}

#endif
