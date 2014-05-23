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
#include <iostream>
#include <string>

/*
	Using styleguide from http://www.gotw.ca/publications/c++cs.htm
	Consulted http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
	heavily throughout the course of this assignment
*/

int main (int argc, char *argv[]) {
	// max length of client input
	const int MAXLEN = 256;

	// check for correct usage
	if (argc < 3) {
		std::cerr << "usage : " << argv[0] << " <server name/ip> <server port>" << std::endl;
		exit (0);
	}

	// obtain a socket descriptor
	int sock;
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		std::cerr<< "socket error" << std::endl;
		exit(1);
	}

	// pack local sockaddr_in with correct information before calling bind
	struct sockaddr_in my_addr;
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = 0;
	my_addr.sin_addr.s_addr = INADDR_ANY;

	// bind the address to the socket
	if (bind(sock, (const struct sockaddr *) (&my_addr), sizeof(struct sockaddr_in)) < 0) {
		std::cerr<< "bind error" << std::endl;
		exit (2);
	}

	struct sockaddr_in server_address;

	unsigned short portnum;
	if (sscanf(argv[2], "%hu", &portnum) < 1) {
		std::cerr<< "sscanf error" << std::endl;
	}

	// obtain the necessary information about the server
	struct addrinfo *res;
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	if (getaddrinfo(argv[1], NULL, &hints, &res) != 0) {
		std::cerr<< "getaddrinfo error" << std::endl;
		exit (3);
	}

    // obtain address from response
	struct addrinfo *temp;
	for (temp = res; temp != NULL; temp = temp->ai_next) {
		if (temp->ai_family == AF_INET) {
			memcpy (&server_address, temp->ai_addr, sizeof(struct sockaddr_in));
			break;
		}
	}

	// set the correct options for the server
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons (portnum);

	char message[MAXLEN];
	char client_input[MAXLEN - 4];
	while (true) {
		bool stop = false;

		// parse client input for special instructions
		if (fgets(client_input, MAXLEN - 4, stdin) == NULL) {
			strcpy(message, "STOP_SESSION");
			stop = true;
		} else if (strcmp(client_input, "STOP\n") == 0) {
			strcpy(message, "STOP");
			stop = true;
	    } else {
			strcpy(message, "GET ");
			strcat(message, client_input);
		}

		// send message and check for truncation
		int sent;
		if ((sent = sendto(sock, message, strlen(message) + 1, 0, (struct sockaddr *) &server_address, sizeof(server_address))) < strlen(message) + 1) {
			std::cerr<< "Message truncated" << std::endl;
		}
		// if stop command was issued, exit
		if (stop) break;

		// receive reply from server
		memset(message, 0, MAXLEN);
		recvfrom(sock, message, MAXLEN, 0, NULL, NULL);

		// convert received char array to string for processing
		std::string received(message);

		// check for errors
		std::string::size_type has_error = received.find("ERROR", 0);
  		if (has_error != std::string::npos) {
  			// parse error from server
  			has_error = received.find("INVALID", 6);
  			if (has_error != std::string::npos) {
  				std::cerr << "error: invalid client_input" << std::endl;
  			} else {
  				std::cerr << "error: " << client_input;
  			}
  		} else {
  			// output valid message from server
			std::cout << message << std::endl;
		}
	}

	// close socket
	close(sock);
	return 0;

}
