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

//TODO I should really use some json lib (probably yajl as its used by i3status)
//TODO I hate all json libs... I should make my own

int main(int argc, char** argv) {
	int ipc[2];
	if(pipe(ipc) == -1) {
		perror("error in pipe()");
		return 1;
	}
	__pid_t i3status_pid = fork();
	if(i3status_pid == -1) {
		perror("error in fork()\n");
		return 1;
	}
	if(i3status_pid) {
		//parent
		close(ipc[1]);
		int readbytes;
		volatile char* buffer = (char*)malloc(BUFFER_SIZE);
		if(buffer == NULL) {
			perror("error in malloc()");
			return 1;
		}
		struct thread_data* td = (struct thread_data*)malloc(sizeof(struct thread_data));
		td->buffer = buffer;
		readbytes = read(ipc[0], buffer, BUFFER_SIZE);
		buffer[strlen(buffer) - 1] = '\0';
		td->readbytes = &readbytes;
		td->socket_path = (char*)malloc(readbytes);
		if(td->socket_path == NULL) {
			perror("error in malloc()");
			return 1;
		}
		strcpy(td->socket_path, buffer);
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
		pthread_t thread_id = 0;
		if(pthread_create(&thread_id, NULL, threadFunc, td) == -1) {
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
		//main loop
		while(!terminate && readbytes != -1) {
			readbytes = read(ipc[0], buffer, BUFFER_SIZE);
			readbytes += prependRate(buffer, readbytes + 1, &mutex);
			buffer[readbytes] = '\0';
			pthread_mutex_lock(&mutex);
			write(1, buffer, readbytes - 2);
			pthread_mutex_unlock(&mutex);
		}
		if(!waitpid(i3status_pid, NULL, WNOHANG)) {
			if(kill(i3status_pid, SIGTERM)) {
				perror("error in kill()");
				fprintf(stderr, "i3status PID was: %i\nit may be required to kill it manually\n", i3status_pid);
				return 1;
			}
		}
	} else {
		//child
		close(ipc[0]);
		dup2(ipc[1], 1);
		{
			pid_t tmp_pid = fork();
			if(!tmp_pid) {
				printf("executing i3 --get-socketpath");
				if(execl("/usr/bin/i3", "/usr/bin/i3", "--get-socketpath", '\0') == -1) {
					perror("error in execv(i3)");
					return 1;
				}
				return 0;
			}
			waitpid(tmp_pid, NULL, 0);
		}
		argv[0] = "i3status";
		if(execv("/usr/bin/i3status", argv) == -1) {
			perror("error in execv(i3status)");
			return 1;
		}
	}
	return 0;
}

void threadFunc(void* arg) {
	struct thread_data* td = (struct thread_data*)arg;
	volatile char* buffer = td->buffer;
	pthread_mutex_t* mutex = td->mutex;
	struct sockaddr_un addr;
	addr.sun_family = AF_LOCAL;
	strcpy(addr.sun_path, td->socket_path);
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
	char received_payload[1024];
	char payload[] = "[\"window\", \"binding\"]";
	header.size = strlen(payload);
	i3_ipc_header_t received_header;
	write(sockfd, &header, sizeof(i3_ipc_header_t));
	write(sockfd, payload, strlen(payload));
	while(1) {
		recv(sockfd, &received_header, sizeof(i3_ipc_header_t), 0);
		recv(sockfd, received_payload, 1024, 0);
		if((received_header.type ^ I3_IPC_EVENT_MASK) == I3_IPC_EVENT_SHUTDOWN) {
			terminate = 1;
			shutdown(sockfd, SHUT_RDWR);
			break;
		}
		pthread_mutex_lock(mutex);
		write(1, buffer, *td->readbytes - 2);
		pthread_mutex_unlock(mutex);
		received_payload[0] = '\0';
	}
	pthread_exit(0);
}

int prependRate(char* buffer, int buffer_len, pthread_mutex_t* mutex) {
	//get up/down rate and prepend to buffer

	struct ifaddrs* if_list;
	if(getifaddrs(&if_list) == -1) {
		perror("error in getifaddrs()");
		return 0;
	}
	struct rtnl_link_stats* stats;
	struct ifaddrs* if_curr = if_list;
	static unsigned int last_tx_bytes;
	static unsigned int last_rx_bytes;
	unsigned int tx_bytes = 0;
	unsigned int rx_bytes = 0;
	//TODO customization of interfaces to monitor; exclude bridges/tunnels; abillity to monitor multiple interfaces seperately
	for(if_curr = if_list; if_curr != NULL; if_curr = if_curr->ifa_next) {
		if(if_curr->ifa_addr->sa_family == AF_PACKET && if_curr->ifa_flags & IFF_UP && !(if_curr->ifa_flags & IFF_LOOPBACK)) {
			//only do this for interfaces that are up AND not loopback
			stats = (struct rtnl_link_stats*)if_curr->ifa_data;
			tx_bytes += stats->tx_bytes;
			rx_bytes += stats->rx_bytes;
		}
	}
	freeifaddrs(if_list);

	double tx = tx_bytes - last_tx_bytes;
	double rx = rx_bytes - last_rx_bytes;
	last_tx_bytes = tx_bytes;
	last_rx_bytes = rx_bytes;
	static time_t curr_time = 0;
	time_t last_time = curr_time;
	curr_time = time(0);
	int interval = curr_time - last_time;
	interval = interval ? interval : 1;
	tx /= interval;
	rx /= interval;
	//tx/rx is now bytes per second
	tx /= 1024;
	rx /= 1024;

	char* tx_unit = "kib/s↑";
	char* rx_unit = "kib/s↓";
	if(tx > 1024) {
		tx /= 1024;
		tx_unit = "mib/s↑";
	}
	if(rx > 1024) {
		rx /= 1024;
		rx_unit = "mib/s↓";
	}
	char rate_str[256] = "";
	sprintf(rate_str, ",[{\"full_text\":\"%.2f%s %.2f%s\"},", rx, rx_unit, tx, tx_unit);
	int len = strlen(rate_str);
	pthread_mutex_lock(mutex);
	memmove(buffer + len - 2, buffer, buffer_len);
	memcpy(buffer, rate_str, len);
	pthread_mutex_unlock(mutex);
	return len;
}
