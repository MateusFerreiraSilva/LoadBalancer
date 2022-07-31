// #include <iostream>
#include <string>
#include <vector>
// #include <map>
#include <stdio.h>
#include <string.h>
// #include <signal.h>
// #include <sys/signalfd.h>
// #include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h> // sockaddr_in
#include <fcntl.h>
// #include <errno.h>
#include <arpa/inet.h>
#include <poll.h>

#ifndef LOAD_BALANCER
#define LOAD_BALANCER

using namespace std;

#define BUFF_SIZE 65536
#define SERVER_FD 0
#define FIRST_CONNECTION 1
#define MAX_CONNECTIONS 100
#define POLLFDS FIRST_CONNECTION + MAX_CONNECTIONS
#define MAX_CONTEXTS MAX_CONNECTIONS / 2

class LoadBalancer {
    private:
        int masterSocket;
        struct sockaddr_in address;
        const int port = 8080;
        const int backlog = 10;
        const int REUSEADDR = 1;
        const int secondsToTimeout = 1;
        int nextServer = 0;

        struct pollfd pollfds[MAX_CONNECTIONS + 1];
        vector<struct context> contexts;
        int totalContexts = 0;

        const vector<pair<string, int>> serversInfo {
            make_pair("127.0.0.1", 8081),
            make_pair("127.0.0.1", 8082)
        };
        vector<struct sockaddr_in> serversAddresses;

        void setNonBlocking(int socket);
        void setReuseAddr(int socket);
        int getFreePoolfdIndex();
        struct pollfd* getPoolfd(int index);
        void init();
        void startListen();
        void connectServers();
        void acceptConnections();
        void handleConnections();
        void processAction(struct context* ctx);
        void destroyContext(struct context* ctx);
        void createContext(int clientSocket);
        // struct context* createContext(int index, int socket, short* events_out);
        void disconnectClient(int socket);
        void createServerConnection(struct context* ctx);
        void addServerToContext(struct context* ctx, int serverSocket);
        void setRecvTimeout(int socket);
        int getServerIndex();
        void passRequest(int clientSocket);
        void recvHttpRequest(struct context* ctx);
        void sendHttpRequest(struct context* ctx);
        void recvHttpResponse(struct context* ctx);
        void sendHttpResponse(struct context* ctx);

        void handle_error(const char* error);

    public:
        LoadBalancer();
        void run();
};

enum State {
    Idle,
    ReceivingRequest,
    SendingRequest,
    ReceivingResponse,
    SendingResponse,
    Done
};

struct context {
    public:
        int contextIndex;
        int serverIndex;
        int clientSocket;
        int serverSocket;
        int clientPoolfdIndex;
        int serverPoolfdIndex;
        State state;
        char buffer[BUFF_SIZE];
        size_t bufferUseSize; // buffer size with amount of bytes currently in use
        char* bufferEnd;
        bool inUse;
};

#endif