#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h> // sockaddr_in
#include <fcntl.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fstream>

#ifndef LOAD_BALANCER
#define LOAD_BALANCER

using namespace std;

#define BUFF_SIZE 65536
#define SERVER_FD 0
#define FIRST_CONNECTION 1
#define MAX_CONNECTIONS 10000 * 2
#define POLLFDS FIRST_CONNECTION + MAX_CONNECTIONS
#define MAX_CONTEXTS MAX_CONNECTIONS / 2

enum RoutingMethod {
    RoundRobin,
    LeastConnected
};

enum State {
    ReceivingRequest,
    SendingRequest,
    ReceivingResponse,
    SendingResponse,
    Done,
    Error
};

struct context {
    public:
        int contextIndex;
        int serverIndex;
        int clientSocket;
        int serverSocket;
        int clientPoolfdIndex;
        int serverPoolfdIndex;
        bool haveConnectionWithServer;
        int tries;
        State state;
        char buffer[BUFF_SIZE];
        size_t bufferUseSize; // buffer size with amount of bytes currently in use
        char* bufferEnd;
        bool inUse;
};

class LoadBalancer {
    private:
        const string configFile = "./config";
        int masterSocket;
        struct sockaddr_in address;
        const int port = 8080;
        const int backlog = 10;
        const int REUSEADDR = 1;
        const int secondsToTimeout = 1;
        RoutingMethod routingMethod;
        int nextServer = 0;
        bool debugMode;
        const int maxTries = 3; // max number of times that a client tries to connect a server before it returns error
        const int maxServerTries = 10; // max number of times that a error can occur in the communication with a server before the server be removed from the list of servers

        struct pollfd pollfds[MAX_CONNECTIONS + 1];
        vector<struct context> contexts;
        int totalContexts = 0;

        vector<pair<string, int>> serversInfo;
        vector<struct sockaddr_in> serversAddresses;
        vector<int> serversTotalConnections;
        vector<int> serversErros;

        void handleError(struct context* ctx, const char* error);
        void setNonBlocking(int socket);
        void setReuseAddr(int socket);
        int getFreePoolfdIndex();
        struct pollfd* getPoolfd(int index);
        void init();
        void readConfigFile();
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
        void createServerSocket(struct context* ctx);
        void addServerToContext(struct context* ctx);
        void setRecvTimeout(int socket);
        int getServerIndex();
        int roundRobin();
        int leastConnected();
        void passRequest(int clientSocket);
        void recvHttpRequest(struct context* ctx);
        void sendHttpRequest(struct context* ctx);
        void recvHttpResponse(struct context* ctx);
        void sendHttpResponse(struct context* ctx);
        void sendErrorResponse(struct context* ctx);
        void sendErrorMessage(struct context* ctx);
        void removeServer(struct context* ctx);

    public:
        LoadBalancer();
        void run();
};

#endif