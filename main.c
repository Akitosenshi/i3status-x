#include "main.h"
#define __USE_POSIX
#define _DEFAULT_SOURCE
#define __USE_MISC
#include <dirent.h>
#include <errno.h>
#include <i3/ipc.h>
#include <ifaddrs.h>
#include <linux/if_link.h>
#include <net/if.h>
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
			readbytes += prependRate(buffer, readbytes + 1);
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
				if(execl("/usr/bin/i3", "/usr/bin/i3", "--get-socketpath", '\0') == -1) {
					perror("error in execv(i3)");
					return 1;
				}
				return 0;
			}
			waitpid(tmpPid, NULL, 0);
		}
		//sleep(1);
		if(execv("/usr/bin/i3status", NULL) == -1) {
			perror("error in execv(i3status)");
			return 1;
		}
	}
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
		return;
	}
	if(connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		perror("error in connect()");
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

int prependRate(char* buffer, int bufferLen) {
	//get up/down rate and prepend to buffer

	struct ifaddrs* ifList;
	if(getifaddrs(&ifList) == -1) {
		perror("error in getifaddrs()");
		return 0;
	}
	struct rtnl_link_stats* stats;
	struct ifaddrs* ifCurr = ifList;
	static unsigned int lastTxBytes;
	static unsigned int lastRxBytes;
	unsigned int txBytes = 0;
	unsigned int rxBytes = 0;
	//TODO customization of interfaces to monitor; exclude bridges/tunnels; abillity to monitor multiple interfaces seperately
	for(ifCurr = ifList; ifCurr != NULL; ifCurr = ifCurr->ifa_next) {
		if(ifCurr->ifa_addr->sa_family == AF_PACKET && ifCurr->ifa_flags & IFF_UP && !(ifCurr->ifa_flags & IFF_LOOPBACK)) {
			//only do this for interfaces that are up AND not loopback
			stats = (struct rtnl_link_stats*)ifCurr->ifa_data;
			txBytes += stats->tx_bytes;
			rxBytes += stats->rx_bytes;
		}
	}
	freeifaddrs(ifList);

	double tx = txBytes - lastTxBytes;
	double rx = rxBytes - lastRxBytes;
	lastTxBytes = txBytes;
	lastRxBytes = rxBytes;
	static time_t currTime = 0;
	time_t lastTime = currTime;
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
	char rateStr[256] = "";
	sprintf(rateStr, ",[{\"full_text\":\"%.2f%s %.2f%s\"},", rx, rxUnit, tx, txUnit);
	int len = strlen(rateStr);
	memmove(buffer + len - 2, buffer, bufferLen);
	memcpy(buffer, rateStr, len);
	return len;
}
