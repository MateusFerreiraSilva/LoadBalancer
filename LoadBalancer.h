// #include <iostream>
#include <string>
#include <vector>
// #include <map>
#include <stdio.h>
#include <string.h>
// #include <signal.h>
// #include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h> // sockaddr_in
// #include <fcntl.h>
// #include <errno.h>
#include <arpa/inet.h>

#ifndef LOAD_BALANCER
#define LOAD_BALANCER

using namespace std;

typedef struct sockaddr_in SocketAddress;

class LoadBalancer {
    private:
        int masterSocket;
        SocketAddress address;
        const int port = 8080;
        const int backlog = 10;
        const int secondsToTimeout = 1;
        int nextServer = 0;

        const vector<pair<string, int>> servers {
            make_pair("127.0.0.1", 8081),
            make_pair("127.0.0.1", 8082)
        };

        vector<SocketAddress> serversAddresses;
        vector<int> serversSockets;

        const int requestDataSize = 1024 * 1024 * 2; // 2 MB
        char* requestData;
        const int responseDataSize = 1024 * 1024 * 2; // 2 MB
        char* responseData;

        int acceptConnection();
        void disconnectClient(int socket);
        void connectServers();
        void createServerConnection(SocketAddress serverAddr);
        void setRecvTimeout(int socket);
        int getServerSocket();
        void passRequest(int clientSocket);
        int recvHttpRequest(int clientSocket);
        void sendHttpRequest(int serverSocket, int sizeToBeSent);
        int recvHttpResponse(int serverSocket);
        void sendHttpResponse(int serverSocket, int sizeToBeSent);

    public:
        LoadBalancer();
        void run();
};

#endif