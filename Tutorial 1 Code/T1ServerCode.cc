#include <netinetin.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <ifaddrs.h>

int main (int argc, char *argv[]) {
	int s;
	unsigned short portnum

	// Grab the portnum if one is provided 
	// Otherwise assign portnum to 0

	if (argc < 2) portnum = 0;
	else portnum = (unsigned short)atoi(argv[1]);

	// Create a socket for UDP

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket");
		exit(0);
	}

	// Structure to provide socket info for bind
	struct sockaddr_in a;
	a.sin_family = AF_INET;
	a.sin_port = htons(portnum);
	a.sin_addr.s_addr = INADDR_ANY;

	if (bind (s, (const struct sockaddr *)(&a), sizeof(struct sockaddr_in)) < 0) {
		perror ("bind");
		exit(0);
	}

	memset (&a, 0, sizeof(struct sockaddr_in));

	// In case you didnt specify the port number, we will get it here

	int addrlen = sizeof(struct sockaddr_in);
	if (getsockname(s, (struct sockaddr *) (&a), &addrlen) < 0) {
		perror ("getsockname");
		exit (0);
	}

	printf ("Connecting to port %hu\n", ntohs(a.sin_port));
	memset(&a, 0, sizeof(struct sockaddr_in));

	char buf[256];

	// Wait on the socket for someone to send something
	recvfrom(s, buf, 256, 0, (struct sockaddr*) (&a), &addrlen);

	printf("Server, received: %s\n", buf);

	memset(buf, 0, 256);
	strcpy(buf, "Hello back");

	int len;
	if ((len = sendto(s, buf, strlen(buf) + 1, 0, (const struct sockaddr *)(&a), sizeof(sturct sockaddr_in))) < strlen(buf) + 1) {
		fprintf(stderr, "Tried to send %d, sent only %d\n", strlen(buf) + 1, len );
	}
	close (s);
	return 0;
}