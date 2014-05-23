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


// UDP CLIENT HANDLER

// Internal logic for handling UDP requests
// Returns 0 on success and non-zero value on error
int handle(int sockfd, const GroupMap * groupMap) {
	char buf[4096];

	while (1) {
		// Listen for and read incoming UDP requests indefinitely
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

		// UDP request received, construct InputBuffer
		buf[l] = '\0';
		std::string bufstr(buf);
		InputBuffer inputBuffer(bufstr);

		while (inputBuffer.next()) {
			// Error case
			if (inputBuffer.error()) {
				std::string errStr = "ERROR_INVALID_INPUT";
				sendto(sockfd, errStr.c_str(), errStr.length(), 0, &clientAddr, clientAddrLen);
				continue;
			}

			// STOP case (stop() == true implies stopSession() == true)
			if (inputBuffer.stop()) {
				return 0;
			}
			// UDP server doesn't need to handle STOP_SESSION
			if (inputBuffer.stopSession()) {
				continue;
			}

			// GET case
			if (inputBuffer.hasGet()) {
				std::string groupId = inputBuffer.getGroupId();
				std::string studentId = inputBuffer.getStudentId();
				// group corresponds to groupMap[groupId]
				GroupMap::const_iterator group = groupMap->find(groupId);
				// Check if groupMap[groupId] exists
				// and groupMap[groupId][studentId] exists
				if (group != groupMap->end() && group->second.find(studentId) != group->second.end()) {
					std::string studentName = group->second.find(studentId)->second;
					sendto(sockfd, studentName.c_str(), studentName.length(), 0, &clientAddr, clientAddrLen);
				} else {
					// groupMap[groupId][studentId] does not exist
					std::stringstream err;
					err << "ERROR_" << groupId << "_" << studentId;
					std::string errStr = err.str();
					sendto(sockfd, errStr.c_str(), errStr.length(), 0, &clientAddr, clientAddrLen);
				}
			}
		}
	}
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
	int soc = socket(AF_INET, SOCK_DGRAM, 0);
	
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

	std::cout << inet_ntoa(addr.sin_addr) << " " << ntohs(addr.sin_port) << std::endl;

	// Step 4: Construct GroupMap
	GroupMap groupMap;
	std::cin >> groupMap;

	int retCode = handle(soc, &groupMap);

	close(soc);
	return retCode;
}
