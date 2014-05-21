#include <stdio.h>
#include <string.h>
#include <sys.types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <iffaddrs.h>

int main (int argc, char *argv[]) {
	if (argc < 3_) {
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
	struct addrinfo *res, hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	if (getaddrinfo (argv[1], NULL, const struct addrinfo *) (&hints), &res) ! = 0) {
		perror("getaddrinfo");
		exit (0);
	}

	struct addrinfo *cai;
	for (cai = res; cai != NULL; cai = cai->ai_next) {
		if (cai->ai_family == AF_INET) {
			printf("server ip: %s\n", inet_nta(((struct sockaddr_in *) (cai->ai_addr)) ->sin_addr));
			memcpy (&sa, cai->ai_addr, sizeof(struct sockaddr_in));
			break;
		}
	}
	// inet_aton(argv[1], &sa.sin_a)
	sa.sin_family = AF_INET;
	sa.sin_port = htons (portnum);

	char buf [256];
	strcpy (buf, "hello");

	printf ("Just before sent to()...\n"); fflush(stdout);

	int len;
	if ((len = sendto(s, buf, strlen(buf) + 1, 0, (const struct sockaddr *(&sa), sizeof(struct sockaddr_in))) < strlen(buf) + 1)) {
		fprintf(stderr, "Tried to send %d, sent only %d\n", strlen(buf) + 1, len);
	}
	memset(buf, 0, 256);
	recvfrom(s, buf, 256, 0, NULL, NULL);
	printf("Client received: %s\n", buf);
	close (s);
	return 0;

}