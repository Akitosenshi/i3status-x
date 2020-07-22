#ifndef MAIN_H
#define MAIN_H

#include <pthread.h>

#define BUFFER_SIZE 1024 * 1024

struct threadData {
	int* readbytes;
	char* buffer;
	char* socketPath;
};

void threadFunc(void*);

#endif
