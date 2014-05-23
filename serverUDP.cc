#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <iostream>
#include <map>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
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

class InputBuffer {
	std::stringstream ss;
	std::string tok;
	std::vector<std::string> get;

public:
	InputBuffer(const std::string & str): ss(str) {}

	bool next() {
		get.clear();

		std::string line;
		if (!std::getline(ss, line)) {
			return false;
		}

		std::stringstream line_ss(line);
		if (!(line_ss >> tok)) {
			tok = "";
		} else if (tok == "GET") {
			while (line_ss >> tok) {
				get.push_back(tok);
			}
		} else {
			tok = trim(line);
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

int handle(int sockfd, GroupMap * groupMap) {
	char buf[4096];

	while (1) {
		sockaddr clientAddr;
		socklen_t clientAddrLen = sizeof(sockaddr);
		int l = recvfrom(sockfd, buf, 4095, 0, &clientAddr, &clientAddrLen);
		if (l < 0) {
			perror("recvfrom:");
			return 1;
		}
		if (!l) {
			return 0;
		} 

		buf[l] = '\0';
		std::string bufstr(buf);
		InputBuffer inputBuffer(bufstr);

		while (inputBuffer.next()) {
			if (inputBuffer.error()) {
				std::string errStr = "ERROR_INVALID_INPUT";
				sendto(sockfd, errStr.c_str(), errStr.length(), 0, &clientAddr, clientAddrLen);
			}

			if (inputBuffer.stop()) {
				return 0;
			}

			if (inputBuffer.hasGet()) {
				std::string groupId = inputBuffer.getGroupId();
				std::string studentId = inputBuffer.getStudentId();
				GroupMap::iterator group = groupMap->find(groupId);
				if (group != groupMap->end() && group->second.find(studentId) != group->second.end()) {
					std::string studentName = group->second.find(studentId)->second;
					sendto(sockfd, studentName.c_str(), studentName.length(), 0, &clientAddr, clientAddrLen);
				} else {
					std::stringstream err;
					err << "ERROR_" << groupId << "_" << studentId;
					std::string errStr = err.str();
					sendto(sockfd, errStr.c_str(), errStr.length(), 0, &clientAddr, clientAddrLen);
				}
			}
		}
	}
}

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
	int soc = socket(AF_INET, SOCK_DGRAM, 0);
	
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

	std::cout << inet_ntoa(addr.sin_addr) << " " << ntohs(addr.sin_port) << std::endl;

	GroupMap groupMap;
	std::cin >> groupMap;

	int retCode = handle(soc, &groupMap);

	close(soc);
	return retCode;
}
