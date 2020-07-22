#include "main.h"
#define __USE_POSIX
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
		td->socketPath = (char*)malloc(readbytes);
		td->readbytes = &readbytes;
		if(td->socketPath == NULL) {
			perror("error in malloc()");
			return 1;
		}
		strcpy(td->socketPath, buffer);
		memset(buffer, 0, readbytes + 1);
		pthread_t threadId = 0;
		if(pthread_create(&threadId, NULL, threadFunc, td) == -1) {
			fprintf(stderr, "error in pthread_create()\n");
			return 1;
		}
		while(!terminate && readbytes != -1) {
			readbytes = read(ipc[0], buffer, BUFFER_SIZE);
			buffer[readbytes] = '\0';
			write(1, buffer, readbytes);
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
			waitpid(tmpPid, NULL, NULL);
		}
		sleep(1);
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
	char payload[] = "[\"window\", \"workspace\", \"binding\"]";
	header.size = strlen(payload);
	i3_ipc_header_t receivedHeader;
	write(sockfd, &header, sizeof(i3_ipc_header_t));
	write(sockfd, payload, strlen(payload));
	printf("THREAD: payload=%s\n", payload);
	while(1) {
		recv(sockfd, &receivedHeader, sizeof(i3_ipc_header_t), 0);
		recv(sockfd, receivedPayload, 1024, 0);
		//printf("\nTHREAD: length=%i\nTHREAD: type=%u\nTHREAD: payload=%s\n", receivedHeader.size, receivedHeader.type, receivedPayload);
		write(1, buffer, *td->readbytes);
		receivedPayload[0] = '\0';
	}
}
