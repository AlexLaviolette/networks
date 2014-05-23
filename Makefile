udp:
	g++ -o client clientUDP.cc
	g++ -o server serverUDP.cc

tcp:
	g++ -o client clientTCP.cc
	g++ -o server serverTCP.cc

clean:
	rm -f client server