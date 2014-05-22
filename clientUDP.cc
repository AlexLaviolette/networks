#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <ifaddrs.h>
#include <unistd.h>

int main (int argc, char *argv[]) {
	const int MAXLEN = 256;

	if (argc < 3) {
		fprintf (stderr, "usage : %s <server name/ip> <server port>\n", argv[0]);
		exit (0);
	}

	int s;
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket");
		exit(0);
	}

	struct sockaddr_in a;

	a.sin_family = AF_INET;
	a.sin_port = 0;
	a.sin_addr.s_addr = INADDR_ANY;

	if (bind(s, (const struct sockaddr *) (&a), sizeof(struct sockaddr_in)) < 0) {
		perror("bind");
		exit (0);
	}

	struct sockaddr_in sa; /* Server address */
	unsigned short portnum;

	if (sscanf(argv[2], "%hu", &portnum) < 1) {
		fprintf(stderr, "sscanf() failed.\n");
	}

	/* user getaddrinfo() to get server IP */
	struct addrinfo *res;
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	if (getaddrinfo(argv[1], NULL, &hints, &res) != 0) {
		perror("getaddrinfo");
		exit (0);
	}

	struct addrinfo *cai;
	for (cai = res; cai != NULL; cai = cai->ai_next) {
		if (cai->ai_family == AF_INET) {
			printf("server ip: %s\n", inet_ntoa(((struct sockaddr_in *) (cai->ai_addr)) ->sin_addr));
			memcpy (&sa, cai->ai_addr, sizeof(struct sockaddr_in));
			break;
		}
	}
	sa.sin_family = AF_INET;
	sa.sin_port = htons (portnum);

	char buf[MAXLEN];
	char input[MAXLEN - 4];
	while (true) {
		bool stop = false;

		if (fgets(input, MAXLEN - 4, stdin) == NULL) {
			strcpy(buf, "STOP_SESSION");
			stop = true;
		} else if (strcmp(input, "STOP\n") == 0) {
			strcpy(buf, "STOP");
			stop = true;
	    } else {
			strcpy(buf, "GET ");
			strcat(buf, input);
		}

		int len;
		if ((len = sendto(s, buf, strlen(buf) + 1, 0, (struct sockaddr *) &sa, sizeof(sa))) < strlen(buf) + 1) {
			fprintf(stderr, "Tried to send %lu, sent only %d\n", strlen(buf) + 1, len);
		}
		if (stop) break;

		memset(buf, 0, MAXLEN);
		recvfrom(s, buf, MAXLEN, 0, NULL, NULL);
		char * error;
  		error = strstr(buf, "ERROR");
  		if (error != NULL) {
  			printf("error: %s\n", input);
  		} else {
			printf("%s\n", buf);
		}
	}
	close(s);
	return 0;

}