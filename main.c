#include "main.h"
#define __USE_POSIX
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <errno.h>
#include <i3/ipc.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

volatile __sig_atomic_t terminate = 0;

int main(int argc, char** argv) {
	struct interfaceByteFiles* ifList = getIfFiles();		//if I am planning on making this app customizable I MUST make this optional
	if(ifList == NULL){
		printf("error in getIfFiles() or no interfaces found");
		return 1;
	}
	FILE* rxFile;
	FILE* txFile;
	{
		char ifName[32] = "enp33s0";
		//TODO autodetect interfaces and monitor one or more (see getifaddrs() in ifaddrs.h)
		//maybe add config/customization
		if(argc > 1) {
			strcpy(ifName, argv[1]);
		}
		char txPath[255];
		char rxPath[255];
		sprintf(txPath, "/sys/class/net/%s/statistics/tx_bytes", ifName);
		sprintf(rxPath, "/sys/class/net/%s/statistics/rx_bytes", ifName);
		rxFile = fopen(rxPath, "r");
		if(rxFile == NULL) {
			perror("error at fopen");
			return 1;
		}
		txFile = fopen(txPath, "r");
		if(txFile == NULL) {
			perror("error at fopen");
			return 1;
		}
	}
	int ipc[2];
	if(pipe(ipc) == -1) {
		perror("error in pipe()");
		return 1;
	}
	__pid_t i3statusPid = fork();
	if(i3statusPid == -1) {
		perror("error in fork()\n");
		return 1;
	}
	if(i3statusPid) {
		//parent
		close(ipc[1]);
		int readbytes;
		volatile char* buffer = (char*)malloc(BUFFER_SIZE);
		if(buffer == NULL) {
			perror("error in malloc()");
			return 1;
		}
		struct threadData* td = (struct threadData*)malloc(sizeof(struct threadData));
		td->buffer = buffer;
		readbytes = read(ipc[0], buffer, BUFFER_SIZE);
		buffer[strlen(buffer) - 1] = '\0';
		td->readbytes = &readbytes;
		td->socketPath = (char*)malloc(readbytes);
		if(td->socketPath == NULL) {
			perror("error in malloc()");
			return 1;
		}
		strcpy(td->socketPath, buffer);
		memset(buffer, 0, readbytes + 1);
		pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
		{
			int ret = pthread_mutex_init(&mutex, NULL);
			if(ret != 0) {
				fprintf(stderr, "Error in pthread_mutex_init(): %i", ret);
				return 1;
			}
		}
		td->mutex = &mutex;
		pthread_t threadId = 0;
		if(pthread_create(&threadId, NULL, threadFunc, td) == -1) {
			fprintf(stderr, "error in pthread_create()\n");
			return 1;
		}
		//eat the garbage
		readbytes = read(ipc[0], buffer, BUFFER_SIZE);
		buffer[readbytes] = '\0';
		write(1, buffer, readbytes);
		readbytes = read(ipc[0], buffer, BUFFER_SIZE);
		buffer[readbytes] = '\0';
		write(1, buffer, readbytes);
		//main lööp
		while(!terminate && readbytes != -1) {
			readbytes = read(ipc[0], buffer, BUFFER_SIZE);
			readbytes += prependRate(buffer, txFile, rxFile, readbytes + 1);
			buffer[readbytes] = '\0';
			pthread_mutex_lock(&mutex);
			write(1, buffer, readbytes - 2);
			pthread_mutex_unlock(&mutex);
		}
		if(!waitpid(i3statusPid, NULL, WNOHANG)) {
			if(kill(i3statusPid, SIGTERM)) {
				perror("error in kill()");
				fprintf(stderr, "i3status PID was: %i\nit may be required to kill it manually\n", i3statusPid);
				return 1;
			}
		}
	} else {
		//child
		close(ipc[0]);
		dup2(ipc[1], 1);
		{
			pid_t tmpPid = fork();
			if(!tmpPid) {
				printf("executing i3 --get-socketpath");
				if(execl("/usr/bin/i3", "i3", "--get-socketpath", '\0') == -1) {
					perror("error in execv(i3)");
					return 1;
				}
				return 0;
			}
			waitpid(tmpPid, NULL, 0);
		}
		sleep(1);
		if(execv("/usr/bin/i3status", NULL) == -1) {
			perror("error in execv(i3status)");
			return 1;
		}
	}
	freeIfList(ifList);
	return 0;
}

void threadFunc(void* arg) {
	struct threadData* td = (struct threadData*)arg;
	volatile char* buffer = td->buffer;
	pthread_mutex_t* mutex = td->mutex;
	struct sockaddr_un addr;
	addr.sun_family = AF_LOCAL;
	strcpy(addr.sun_path, td->socketPath);
	int sockfd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if(sockfd == -1) {
		perror("error in socket()");
		return 1;
	}
	if(connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		perror("error in connect");
		terminate = 1;
		return;
	}

	i3_ipc_header_t header = {
		.magic = {'i', '3', '-', 'i', 'p', 'c'},
		.type = I3_IPC_MESSAGE_TYPE_SUBSCRIBE};
	char receivedPayload[1024];
	char payload[] = "[\"window\", \"binding\"]";
	header.size = strlen(payload);
	i3_ipc_header_t receivedHeader;
	write(sockfd, &header, sizeof(i3_ipc_header_t));
	write(sockfd, payload, strlen(payload));
	while(1) {
		recv(sockfd, &receivedHeader, sizeof(i3_ipc_header_t), 0);
		recv(sockfd, receivedPayload, 1024, 0);
		if((receivedHeader.type ^ I3_IPC_EVENT_MASK) == I3_IPC_EVENT_SHUTDOWN) {
			terminate = 1;
			shutdown(sockfd, SHUT_RDWR);
			break;
		}
		pthread_mutex_lock(mutex);
		write(1, buffer, *td->readbytes - 2);
		pthread_mutex_unlock(mutex);
		receivedPayload[0] = '\0';
	}
	pthread_exit(0);
}

int prependRate(char* buffer, FILE* txFile, FILE* rxFile, int bufferLen) {
	//get up/down rate and prepend to buffer
	static time_t lastTime = 0;
	static time_t currTime = 0;
	static double lastRxBytes = 0;
	static double lastTxBytes = 0;
	static double rxBytes = 0;
	static double txBytes = 0;
	char rxBytesStr[32] = "";
	char txBytesStr[32] = "";
	rewind(rxFile);
	fread(rxBytesStr, 32, 1, rxFile);
	rewind(txFile);
	fread(txBytesStr, 32, 1, txFile);
	lastTxBytes = txBytes;
	lastRxBytes = rxBytes;
	rxBytes = atof(rxBytesStr);
	txBytes = atof(txBytesStr);
	double tx = txBytes - lastTxBytes;
	double rx = rxBytes - lastRxBytes;
	lastTime = currTime;
	currTime = time(0);
	int interval = currTime - lastTime;
	interval = interval ? interval : 1;
	tx /= interval;
	rx /= interval;
	//tx/rx is now bytes per second
	tx /= 1024;
	rx /= 1024;
	char* txUnit = "kib/s↑";
	char* rxUnit = "kib/s↓";
	if(tx > 1024) {
		tx /= 1024;
		txUnit = "mib/s↑";
	}
	if(rx > 1024) {
		rx /= 1024;
		rxUnit = "mib/s↓";
	}
	char rateStr[128] = "";
	sprintf(rateStr, ",[{\"full_text\":\"%.2f%s %.2f%s\"},", rx, rxUnit, tx, txUnit);
	int len = strlen(rateStr);
	memmove(buffer + len - 2, buffer, bufferLen);
	memcpy(buffer, rateStr, len);
	return len;
}

struct interfaceByteFiles* getIfFiles(){		//TODO et ifnames through getifaddrs() and check if its up and a physical interface
	DIR* ifDir = opendir("/sys/class/net");
	struct interfaceByteFiles* list = (struct interfaceByteFiles*)malloc(sizeof(struct interfaceByteFiles));
	struct interfaceByteFiles* curr = list;
	errno = 0;
	struct dirent* entry;
	while(entry = readdir(ifDir)){
		for(int counter = 0; counter < IFNAMECOUNT; ++counter){
			if(strncmp(entry->d_name + 15, IFNAMES[counter], IFNAMELEN[counter]) == 0){
				char pathRx[256] = "";
				char pathTx[256] = "";
				strcpy(pathRx, entry->d_name);
				strcat(pathRx, "/statistics/");
				strcpy(pathTx, pathRx);
				strcat(pathRx, "rx_bytes");
				strcat(pathTx, "tx_bytes");
				curr->rx = fopen(pathRx, "r");
				if(curr->rx == NULL){
					char err[1024] = "error in fopen(rx) on iterface %s";
					sprintf(err, err, entry->d_name);
					perror(err);
					curr->rx = NULL;
					errno = 0;
					continue;
				}
				curr->tx = fopen(pathTx, "r");
				if(curr->tx == NULL){
					char err[1024] = "error in fopen(tx) on iterface %s";
					sprintf(err, err, entry->d_name);
					perror(err);
					curr->rx = NULL;
					curr->tx = NULL;
					errno = 0;
					continue;
				}
				struct interfaceByteFiles* next = (struct interfaceByteFiles*)malloc(sizeof(struct interfaceByteFiles));
				curr->next = next;
				next->prev = curr;
				curr = next;
			}
		}
	}
	if(errno != 0){
		perror("error in readdir()");
		freeIfList(list);
	}
	return list;
}

void freeIfList(struct interfaceByteFiles* list){
	struct interfaceByteFiles* curr;
	while(curr->next){
		curr = list;
		fclose(curr->rx);
		fclose(curr->tx);
		free(list);
		list = curr->next;
		curr->prev = NULL;
		curr->next = NULL;
	}
	list = NULL;
	curr = NULL;
}
