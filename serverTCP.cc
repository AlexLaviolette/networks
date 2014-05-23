#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <iostream>
#include <map>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <vector>
#include "mybind.c"
#include "unistd.h"

#define SELECT_WAIT_SECS 0
#define SELECT_WAIT_MICROSECS 500000

// STRING UTILITIES 

std::string trim(std::string line) {
	int left, right;
	for (left = 0; left < line.length() && isspace(line[left]); left++);
	for (right = line.length() - 1; right >= left && isspace(line[right]); right--);
	if (right >= left) {
		return line.substr(left, right - left + 1);
	}
	return "";
}

bool isNumeric(std::string str) {
	if (!str.length()) {
		return false;
	}
	for (unsigned int i = 0; i < str.length(); i++) {
		if (!isdigit(str[i])) {
			return false;
		}
	}
	return true;
}

std::string & tolower(std::string & str) {
	for (int i = 0; i < str.length(); i++) {
		if (isupper(str[i])) {
			str[i] = tolower(str[i]);
		}
	}
	return str;
}


// GROUP MAP
// student info is stored in this map (groupId -> studentId -> studentName)

typedef std::map<std::string, std::map<std::string, std::string> > GroupMap;

// read stdin input into the groupMap data structure
std::istream & operator>>(std::istream & in, GroupMap & groupMap) {
	std::string line;
	std::string groupId;

	while (std::getline(in, line)) {
		std::stringstream ss(line);
		std::string studentId;
		ss >> studentId >> std::ws;

		if (tolower(studentId) == "group") {
			// this is a group ID declaration
			// the next token will be the group ID
			ss >> groupId;
		} else {
			// extract the first token as studentId
			// the remainder of the line is the studentName
			std::string studentName;
			std::getline(ss, studentName);
			groupMap[groupId][studentId] = studentName;
		}
	}

	return in;
}


// INPUT BUFFER
// Class for translating text sent by the client into server instructions

class InputBuffer {
	std::stringstream ss;
	std::string tok;
	std::vector<std::string> get;

public:
	InputBuffer(const std::string & str): ss(str) {}

	// Read the next command (contained in the next line of ss)
	// If there are no more lines to be read, return false; otherwise return true
	bool next() {
		get.clear();

		std::string line;
		if (!std::getline(ss, line)) {
			return false;
		}

		std::stringstream line_ss(line);
		if (!(line_ss >> tok)) {
			tok = "";
		} else {
			// Tokenize GET commands
			if (tolower(tok) == "get") {
				while (line_ss >> tok) {
					get.push_back(tok);
				}
			}
			tok = trim(tolower(line));
		}
		return true;
	}

	bool stop() const {
		return tok == "stop";
	}
	bool stopSession() const {
		return stop() || (tok == "stop_session");
	}
	bool hasGet() const {
		return isNumeric(getGroupId()) && isNumeric(getStudentId());
	}
	bool error() const {
		return !tok.empty() && !stopSession() && !hasGet();
	}

	std::string getGroupId() const {
		return get.size() == 2 ? get[0] : "";
	}
	std::string getStudentId() const {
		return get.size() == 2 ? get[1] : "";
	}
};


// CLIENT THREAD
// everything that identifies and will be used by a client-serving thread

struct ClientThread {
	pthread_t id;						// thread ID
	int sockfd;							// client socket
	const GroupMap * groupMap;			// pointer to GroupMap
	char * end_session;					// shared memory, flag for STOP signal
	pthread_mutex_t * m_end_session;	// mutex for end_session

	ClientThread(
		int sockfd,
		const GroupMap * groupMap,
		char * end_session,
		pthread_mutex_t * m_end_session
	):
		sockfd(sockfd),
		groupMap(groupMap),
		end_session(end_session),
		m_end_session(m_end_session)
	{}
};

// Internal logic for client threads
void _handle(ClientThread * ct) {
	char buf[4096];

	while (1) {
		// Check whether the STOP signal has been sent
		pthread_mutex_lock(ct->m_end_session);
		int _end_session = *ct->end_session;
		pthread_mutex_unlock(ct->m_end_session);
		if (_end_session) {
			return;
		}

		// Use select() here in order to regularly check whether STOP has been sent
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(ct->sockfd, &fds);
		timeval tv = {SELECT_WAIT_SECS, SELECT_WAIT_MICROSECS};

		int retval = select(ct->sockfd + 1, &fds, NULL, NULL, &tv);
		if (retval < 0) {
			// Error
			perror("Select:");
			return;
		} else if (retval == 0) {
			// Nothing to be read from socket
			continue;
		}

		// Read from client socket and construct InputBuffer
		int l = read(ct->sockfd, buf, 4095);
		buf[l] = '\0';
		std::string bufstr(buf);
		InputBuffer inputBuffer(bufstr);

		while (inputBuffer.next()) {
			// Error case
			if (inputBuffer.error()) {
				std::string errStr = "ERROR_INVALID_INPUT";
				write(ct->sockfd, errStr.c_str(), errStr.length());
				continue;
			}

			// STOP case (stop() == true implies stopSession() == true)
			if (inputBuffer.stop()) {
				// Communicate to other threads that STOP has been sent
				pthread_mutex_lock(ct->m_end_session);
				*ct->end_session = 1;
				pthread_mutex_unlock(ct->m_end_session);
			}
			if (inputBuffer.stopSession()) {
				return;
			}

			// GET case
			if (inputBuffer.hasGet()) {
				std::string groupId = inputBuffer.getGroupId();
				std::string studentId = inputBuffer.getStudentId();
				// group corresponds to groupMap[groupId]
				GroupMap::const_iterator group = ct->groupMap->find(groupId);
				// Check if groupMap[groupId] exists
				// and groupMap[groupId][studentId] exists
				if (group != ct->groupMap->end() && group->second.find(studentId) != group->second.end()) {
					std::string studentName = group->second.find(studentId)->second;
					write(ct->sockfd, studentName.c_str(), studentName.length());
				} else {
					// groupMap[groupId][studentId] does not exist
					std::stringstream err;
					err << "ERROR_" << groupId << "_" << studentId;
					std::string errStr = err.str();
					write(ct->sockfd, errStr.c_str(), errStr.length());
				}
			}
		}
	}
}

// The handler acting as the main method for the client threads
// Most of the work is delegated to _handle
void * handle(void * arg) {
	ClientThread * ct = (ClientThread *) arg;
	_handle(ct);
	close(ct->sockfd);
	return NULL;
}


// SOCKET UTILITIES

// Provides a sockaddr (complete with IP address)
// that can be used with AF_INET socket "soc"
// Returns 0 on success and -1 on error
int getUnboundSockAddr(int soc, sockaddr_in * addr) {
	ifaddrs * addrs;

	// Step 1: Get ifaddrs for all network interfaces
	if (getifaddrs(&addrs) < 0) {
		perror("getifaddrs():");
		return -1;
	}

	// Step 2: Select an open outbound AF_INET interface and get its name
	std::string ifa_name;
	for (ifaddrs * addrs_temp = addrs; addrs_temp != NULL; addrs_temp = addrs_temp->ifa_next) {
		if (addrs_temp->ifa_name == NULL) continue;
		if (!(addrs_temp->ifa_flags & (IFF_UP | IFF_BROADCAST))) continue;
		if (addrs_temp->ifa_flags & IFF_LOOPBACK) continue;
		if (addrs_temp->ifa_addr->sa_family != AF_INET) continue;
		ifa_name = addrs_temp->ifa_name;
		break;
	}

	freeifaddrs(addrs);
	if (!ifa_name.length()) {
		std::cerr << "Could not find appropriate network device" << std::endl;
		return -1;
	}

	// Step 3: Get the IP address for the selected interface
	ifreq ifr;
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, ifa_name.c_str(), IFNAMSIZ-1);
	if (ioctl(soc, SIOCGIFADDR, &ifr) < 0) {
		perror("ioctl:");
		return -1;
	}

	// Step 4: Copy info from ioctl to the provided sockaddr pointer
	*addr = *(sockaddr_in *)(&ifr.ifr_addr);
	return 0;
}


// MAIN

int main() {
	// Step 1: Create socket
	int soc = socket(AF_INET, SOCK_STREAM, 0);
	
	if (soc < 0) {
		perror("Socket:");
		return 1;
	}

	// Step 2: Get the address for an appropriate network interface
	sockaddr_in addr;
	if (getUnboundSockAddr(soc, &addr) < 0) {
		close(soc);
		return 1;
	}

	// Step 3: Bind socket to sockaddr
	if (mybind(soc, &addr) < 0) {
		perror("Bind:");
		close(soc);
		return 1;
	}

	// Step 4: Open the socket to listen for incoming requests
	if (listen(soc, 1000) < 0) {
		perror("Listen:");
		close(soc);
		return 1;
	}

	std::cout << inet_ntoa(addr.sin_addr) << " " << ntohs(addr.sin_port) << std::endl;

	// Step 5: Construct GroupMap
	GroupMap groupMap;
	std::cin >> groupMap;

	char end_session = 0;
	pthread_mutex_t m_end_session;
	pthread_mutex_init(&m_end_session, NULL);

	std::vector<ClientThread *> threads;
	int retCode = 0;

	while (1) {
		// Step 6: Check whether STOP has been sent
		pthread_mutex_lock(&m_end_session);
		int _end_session = end_session;
		pthread_mutex_unlock(&m_end_session);
		if (_end_session) {
			break;
		}

		// Step 7: Wait for client requests
		// (select() is used here to continue listening for requests
		// even while currently serving other clients, and to check
		// whether STOP has been sent)
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(soc, &fds);
		timeval tv = {SELECT_WAIT_SECS, SELECT_WAIT_MICROSECS};

		int retval = select(soc + 1, &fds, NULL, NULL, &tv);
		if (retval < 0) {
			// Error
			perror("Select:");
			retCode = 1;
			break;
		} else if (retval == 0) {
			// No client requests, go back to the beginning of the loop
			continue;
		}

		// Step 7: A client wants to connect, accept the connection
		int clientSoc = accept(soc, NULL, NULL);
		if (clientSoc < 0) {
			perror("Accept:");
			retCode = 1;
			break;
		}

		// Step 8: Create new client thread for serving the new client
		ClientThread * ct = new ClientThread(clientSoc, &groupMap, &end_session, &m_end_session);
		if (pthread_create(&(ct->id), NULL, handle, ct) != 0) {
			delete ct;
		} else {
			threads.push_back(ct);
		}
	}

	// Step 9: Cleanup, join all client threads
	close(soc);
	for (unsigned int i = 0; i < threads.size(); ++i) {
		pthread_join(threads[i]->id, NULL);
		delete threads[i];
	}
	pthread_mutex_destroy(&m_end_session);
	return retCode;
}
