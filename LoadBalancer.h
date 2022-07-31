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

#define SERVER_FD 0
#define FIRST_CONNECTION 1
#define MAX_CONNECTIONS 20
#define POLLFDS FIRST_CONNECTION + MAX_CONNECTIONS

class LoadBalancer {
    private:
        int masterSocket;
        struct sockaddr_in address;
        const int port = 8080;
        const int backlog = 10;
        const int REUSEADDR = 1;
        const int secondsToTimeout = 1;
        int nextServer = 0;

        // sigset_t sigset;
        // struct signalfd_siginfo siginfo;
        struct pollfd pollfds[MAX_CONNECTIONS + 2];
        struct context* connections[MAX_CONNECTIONS];
        int total_connections = 0;

        const vector< pair<string, int> > servers {
            make_pair("127.0.0.1", 8081),
            make_pair("127.0.0.1", 8082)
        };

        vector<struct sockaddr_in> serversAddresses;

        const int requestDataSize = 1024 * 1024 * 2; // 2 MB
        char* requestData;
        const int responseDataSize = 1024 * 1024 * 2; // 2 MB
        char* responseData;

        void init();
        void startListen();
        void setNonBlocking(int socket);
        void setReuseAddr(int socket);
        void acceptConnection();
        void handleConnections();
        int handleConn(struct context* ctx, short revents, short* events_out, int* connection_completed);
        void destroyConnection(struct context* ctx);
        struct context* createContext(int index, int socket, short* events_out);
        void disconnectClient(int socket);
        void connectServers();
        struct context* createServerConnection(struct sockaddr_in serverAddr);
        void setRecvTimeout(int socket);
        int getServerIndex();
        void passRequest(int clientSocket);
        int recvHttpRequest(struct context* ctx, short* events_out, int* connection_completed);
        void sendHttpRequest(struct context* ctx, short* events_out, int* connection_completed);
        int recvHttpResponse(int serverSocket);
        void sendHttpResponse(int serverSocket, int sizeToBeSent);
        struct context* createConnection(int socket);

    public:
        LoadBalancer();
        void run();
        void handle_error(const char* error);
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
        int fd;
        int serverIndex;
        State state;
        char buf[65536];
        size_t bytes;
        char* buf_end;
        bool valid;
};



#endif