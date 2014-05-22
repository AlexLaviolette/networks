#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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

typedef std::map<std::string, std::map<std::string, std::string> > GroupMap;

std::istream & operator>>(std::istream & in, GroupMap & groupMap) {
	std::string line;
	std::string groupId;
	while (std::getline(in, line)) {
		std::stringstream ss(line);
		if (line.find_first_of("Group") == 0) {
			ss >> groupId >> groupId;
		} else {
			std::string studentId, studentName;
			ss >> studentId >> std::ws;
			std::getline(ss, studentName);
			groupMap[groupId][studentId] = studentName;
		}
	}
	return in;
}

struct ClientThread {
	pthread_t id;
	int sockfd;
	GroupMap * groupMap;
	char * end_session;
	pthread_mutex_t * m_end_session;

	ClientThread(
		int sockfd,
		GroupMap * groupMap,
		char * end_session,
		pthread_mutex_t * m_end_session
	):
		sockfd(sockfd),
		groupMap(groupMap),
		end_session(end_session),
		m_end_session(m_end_session)
	{}
};

class InputBuffer {
	std::stringstream ss;
	std::string tok;
	std::string last_tok;
	std::vector<std::string> get;
	bool end;

public:
	InputBuffer(): end(false) {}

	bool next() {
		if (!(ss >> tok)) {
			return false;
		}
		if (last_tok.length()) {
			tok = last_tok + tok;
			last_tok = "";
		}
		if (!end && !ss.peek()) {
			last_tok = tok;
			tok = "";
			return false;
		}
		if (tok == "GET") {
			get.clear();
			get.push_back(tok);
		} else if (!get.empty() && get.size() < 3) {
			get.push_back(tok);
		} else {
			get.clear();
		}
		return true;
	}

	bool stop() const {
		return tok == "STOP";
	}
	bool stopSession() const {
		return stop() || (tok == "STOP_SESSION");
	}
	bool hasGet() const {
		return get.size() == 3;
	}
	bool error() const {
		return !stopSession() && get.empty();
	}

	std::string getGroupId() const {
		return get[1];
	}
	std::string getStudentId() const {
		return get[2];
	}

	InputBuffer & operator<<(const std::string & str) {
		ss << str;
		return *this;
	}

	void endRead() {
		end = true;
	}
};

void _handle(ClientThread * ct) {
	InputBuffer inputBuffer;
	char buf[4096];

	while (1) {
		int l = read(ct->sockfd, buf, 4095);
		if (l) {
			buf[l] = '\0';
			std::string bufstr(buf);
			inputBuffer << bufstr;
		} else {
			inputBuffer.endRead();
		}

		while (inputBuffer.next()) {
			if (inputBuffer.error()) {
				std::string errStr = "ERROR_INVALID_INPUT";
				write(ct->sockfd, errStr.c_str(), errStr.length());
				continue;
			}

			if (inputBuffer.stop()) {
				pthread_mutex_lock(ct->m_end_session);
				*ct->end_session = 1;
				pthread_mutex_unlock(ct->m_end_session);
			}
			if (inputBuffer.stopSession()) {
				return;
			}

			if (inputBuffer.hasGet()) {
				std::string groupId = inputBuffer.getGroupId();
				std::string studentId = inputBuffer.getStudentId();
				GroupMap::iterator group = ct->groupMap->find(groupId);
				if (group != ct->groupMap->end() && group->second.find(studentId) != group->second.end()) {
					std::string studentName = group->second.find(studentId)->second;
					write(ct->sockfd, studentName.c_str(), studentName.length());
				} else {
					std::stringstream err;
					err << "ERROR_" << groupId << "_" << studentId;
					std::string errStr = err.str();
					write(ct->sockfd, errStr.c_str(), errStr.length());
				}
			}
		}

		if (!l) {
			return;
		}
	}
}

void * handle(void * arg) {
	ClientThread * ct = (ClientThread *) arg;
	_handle(ct);
	close(ct->sockfd);
	return NULL;
}

char end_session = 0;
pthread_mutex_t m_end_session;

int getUnboundSockAddr(int soc, sockaddr_in * addr) {
	ifaddrs * addrs;
	if (getifaddrs(&addrs) < 0) {
		perror("getifaddrs():");
		return -1;
	}

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

	ifreq ifr;
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, ifa_name.c_str(), IFNAMSIZ-1);
	if (ioctl(soc, SIOCGIFADDR, &ifr) < 0) {
		perror("ioctl:");
		return -1;
	}

	*addr = *(sockaddr_in *)(&ifr.ifr_addr);
	return 0;
}


int main() {
	int soc = socket(AF_INET, SOCK_STREAM, 0);
	
	if (soc < 0) {
		perror("Socket:");
		return 1;
	}

	sockaddr_in addr;
	if (getUnboundSockAddr(soc, &addr) < 0) {
		close(soc);
		return 1;
	}

	if (mybind(soc, &addr) < 0) {
		perror("Bind:");
		close(soc);
		return 1;
	}

	if (listen(soc, 1000) < 0) {
		perror("Listen:");
		close(soc);
		return 1;
	}

	std::cout << inet_ntoa(addr.sin_addr) << " " << ntohs(addr.sin_port) << std::endl;

	GroupMap groupMap;
	std::cin >> groupMap;

	std::vector<ClientThread *> threads;
	pthread_mutex_init(&m_end_session, NULL);
	char _end_session = 0;
	timeval tv = {0, 500000};

	while (!_end_session) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(soc, &fds);

		int retval = select(soc + 1, &fds, NULL, NULL, &tv);
		if (retval < 0) {
			perror("Select:");
			break;
		} else if (retval > 0) {
			int clientSoc = accept(soc, NULL, NULL);
			if (clientSoc < 0) {
				perror("Accept:");
				break;
			}

			ClientThread * ct = new ClientThread(clientSoc, &groupMap, &end_session, &m_end_session);
			if (pthread_create(&(ct->id), NULL, handle, ct) != 0) {
				delete ct;
			} else {
				threads.push_back(ct);
			}
		}

		pthread_mutex_lock(&m_end_session);
		_end_session = end_session;
		pthread_mutex_unlock(&m_end_session);
	}

	close(soc);
	for (unsigned int i = 0; i < threads.size(); ++i) {
		pthread_join(threads[i]->id, NULL);
		delete threads[i];
	}
	pthread_mutex_destroy(&m_end_session);
}
