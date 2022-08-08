#include "LoadBalancer.h"

LoadBalancer::LoadBalancer() {
    contexts.resize(MAX_CONTEXTS);
}

void LoadBalancer::setNonBlocking(int socket) {
    int flags = fcntl(socket, F_GETFD, 0); // get current flags
    if (fcntl(socket, F_SETFD, flags | O_NONBLOCK))
        handleError(NULL, "fcntl");
}

void LoadBalancer::setReuseAddr(int socket) {
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &REUSEADDR, sizeof(REUSEADDR)) < 0)
        handleError(NULL, "Erro while setting master socket options");
}

int LoadBalancer::getFreePoolfdIndex() {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        // check if is free
        if (pollfds[FIRST_CONNECTION +  i].fd < 0) return FIRST_CONNECTION +  i;
    }
    
    return -1; // if is full
}

struct pollfd* LoadBalancer::getPoolfd(int index) {
    if (index >= 0)
        return &pollfds[index];

    return NULL;
}

void LoadBalancer::run() {
    init();

    while(true) {
        for (int i = 0; i < POLLFDS; i++) pollfds[i].revents = 0;

        if (poll(pollfds, POLLFDS, -1) < 0) { // third arg timeout, if -1 then infinite
            handleError(NULL, "poll");
            continue;
        }

        acceptConnections();
        handleConnections();
    }
}

void LoadBalancer::readConfigFile() {
    ifstream file(configFile);
    string word;
    if (file.is_open()) {

        while (file >> word) {
            if (word == "routing-method") {
                string method;
                file >> method;
                routingMethod = method == "LeastConnected" ? LeastConnected : RoundRobin;
            } else if (word == "servers") {
                int n, portNumber;
                string addr;

                file >> n;
                for (int i = 0; i < n; i++) {
                    file >> addr >> portNumber;
                    serversInfo.push_back(make_pair(addr, portNumber));
                }
            } else if (word == "debugMode") {
                bool isDebugMode;
                file >> isDebugMode;
                debugMode = isDebugMode;
            }
        }

        file.close();
    }
}

void LoadBalancer::init() {
    readConfigFile();
    startListen();

    // put master socket in pollFds
    pollfds[SERVER_FD].fd = masterSocket;
    pollfds[SERVER_FD].events = POLLIN;

    for (int i = FIRST_CONNECTION; i < POLLFDS; i++) {
        pollfds[i].fd = -1;
        pollfds[i].events = 0;
    }

    for (int i = 0; i < MAX_CONTEXTS; i++)
        contexts[i].inUse = false;    
    
    totalContexts = 0;

    connectServers();

    printf("\n------------ load balancer running on port %d ------------\n", port);
}

void LoadBalancer::startListen() {
    if ((masterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        handleError(NULL, "In socket");
        return;
    }

    setNonBlocking(masterSocket);
    setReuseAddr(masterSocket);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(masterSocket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        handleError(NULL, "In bind");
        return;
    }
    if (listen(masterSocket, backlog) < 0) {
        handleError(NULL, "In listen");
        return;
    }
}

void LoadBalancer::connectServers() {
    for (int i = 0; i < serversInfo.size(); i++) {
        const char* serverIpAddress = serversInfo[i].first.c_str();
        int serverPortNumber = serversInfo[i].second;

        struct sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(serverPortNumber);

        // Convert IPv4 and IPv6 addresses from text to binary form
        if (inet_pton(AF_INET, serverIpAddress, &serverAddr.sin_addr) <= 0) {
            handleError(NULL, "\nInvalid address/ Address not supported \n");
            return;
        }

        serversAddresses.push_back(serverAddr);
        serversErros.push_back(0);
        serversTotalConnections.push_back(0);
    }
}

void LoadBalancer::acceptConnections() {
    if (pollfds[SERVER_FD].revents & POLLERR) {
        handleError(NULL, "server failure");
        return;
    }
    
    if (pollfds[SERVER_FD].revents & POLLIN) { // check if have activity on the laodbalancer listener
        struct sockaddr_in clientAddress;
        int clientAddrlen = sizeof(clientAddress);
        int clientSocket = accept(masterSocket, (struct sockaddr *)&clientAddress, (socklen_t *)&clientAddrlen);
        // printf("\nNew connection , socket fd is %d , ip is : %s , port : %d \n", socket, inet_ntoa(clientAddress.sin_addr), ntohs(clientAddress.sin_port));
        if (clientSocket < 0) {
            handleError(NULL, "Error acception connection");
            return;
        }

        setNonBlocking(clientSocket);
        createContext(clientSocket);
    }
}

void LoadBalancer::createContext(int clientSocket) {
    if (totalContexts == MAX_CONTEXTS)
        return; // there no free space

    for (int i = 0; i < MAX_CONTEXTS; i++) { // find where to place the context
        if (contexts[i].inUse == false) {
            contexts[i].inUse = true;
            contexts[i].contextIndex = i;
            contexts[i].serverIndex = -1; // no server set
            contexts[i].clientSocket = clientSocket;
            contexts[i].serverSocket = -1; // no server set
            contexts[i].haveConnectionWithServer = false;
            contexts[i].tries = 0;
            contexts[i].state = ReceivingRequest;
            contexts[i].bufferUseSize = 0;
            contexts[i].bufferEnd = NULL;

            int clientPoolfdIndex = getFreePoolfdIndex();
            contexts[i].clientPoolfdIndex = clientPoolfdIndex;
            pollfds[clientPoolfdIndex].fd = clientSocket;
            pollfds[clientPoolfdIndex].events = POLLIN;

            contexts[i].serverPoolfdIndex = -1;

            totalContexts++;
            break;
        }
    }
}

void LoadBalancer::handleConnections() {
    for (int i = 0; i < MAX_CONTEXTS; i++) {
        struct context* ctx = &contexts[i];
        // check if the context is not in use then skip it
        if (ctx->inUse == false)
            continue;

        const int fdsSize = 2;
        struct pollfd* fds[fdsSize];
        fds[0] = getPoolfd(ctx->clientPoolfdIndex);
        fds[1] = getPoolfd(ctx->serverPoolfdIndex);

        for (int j = 0; j < fdsSize; j++) {
            if (fds[j] == NULL)
                continue; // if not valid fd, go to next

            int revents = fds[j]->revents;
            if (revents & POLLERR) {
                handleError(ctx, "socket failure");
                return;
            }
            
            if (revents) {
                processAction(ctx);
            }
        }
    }
}

void LoadBalancer::processAction(struct context* ctx) {
    switch (ctx->state) {
        case Error:
            sendErrorResponse(ctx);
            break;
        case ReceivingRequest:
            recvHttpRequest(ctx);
            break;
        case SendingRequest:
            sendHttpRequest(ctx);
            break;
        case ReceivingResponse:
            recvHttpResponse(ctx);
            break;
        case SendingResponse:
            sendHttpResponse(ctx);
            break;
        default: // Done
            destroyContext(ctx);
    };
}

void LoadBalancer::destroyContext(struct context* ctx) {
    struct context* c = &contexts[ctx->contextIndex];

    c->inUse = false;
    close(c->clientSocket);
    close(c->serverSocket);

    if (ctx->serverIndex != -1)
        serversTotalConnections[ctx->serverIndex]--;

    int clientPoolfdIndex = c->clientPoolfdIndex;
    if (clientPoolfdIndex >= 0) {
        pollfds[clientPoolfdIndex].fd = -1;
        pollfds[clientPoolfdIndex].events = 0;
    }
    
    int serverPoolfdIndex = c->serverPoolfdIndex;
    if (serverPoolfdIndex >= 0) {
        pollfds[serverPoolfdIndex].fd = -1;
        pollfds[serverPoolfdIndex].events = 0;
    }
    
    totalContexts--;
}

void LoadBalancer::createServerConnection(struct context* ctx) {
    if (ctx->serverIndex == -1) {
        handleError(ctx, "Server index");
        return;
    }

    struct sockaddr_in serverAddr = serversAddresses[ctx->serverIndex]; // TO DO function to access it
    if (ctx->serverSocket < 0)
        createServerSocket(ctx);

    const int connection = connect(ctx->serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if (connection == EINPROGRESS || connection == EALREADY)
        return;
    else if (connection < 0) {
        handleError(ctx, "Connection Failed");
        return;
    }

    addServerToContext(ctx);

    if (ctx->serverIndex != -1)
        serversTotalConnections[ctx->serverIndex]++;
}

void LoadBalancer::createServerSocket(struct context* ctx) {
    if (ctx->serverIndex == -1) {
        handleError(ctx, "Create server socket, getting index");
        return;
    }
    
    struct sockaddr_in serverAddr = serversAddresses[ctx->serverIndex]; // TO DO function to access it
    int serverSocket;
    if ((serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        handleError(ctx, "creating server socket");
    }
    
    setRecvTimeout(serverSocket);
    setNonBlocking(serverSocket);
    ctx->serverSocket = serverSocket;
}

void LoadBalancer::addServerToContext(struct context* ctx) {
    int serverPoolfdIndex = getFreePoolfdIndex();
    ctx->serverPoolfdIndex = serverPoolfdIndex;
    pollfds[serverPoolfdIndex].fd = ctx->serverSocket;
    pollfds[serverPoolfdIndex].events = POLLOUT;
    ctx->haveConnectionWithServer = true;
}

void LoadBalancer::setRecvTimeout(int socket) {
    struct timeval tv;
    tv.tv_sec = secondsToTimeout;
    tv.tv_usec = 0;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
}

int LoadBalancer::getServerIndex() {
    switch (routingMethod) {
        case LeastConnected:
            return leastConnected();
        default:
            return roundRobin();
    }
}

int LoadBalancer::roundRobin() {
    int serverIdx = nextServer;
    const int numOfServers = (int) serversAddresses.size();

    for(int i = 0; i < numOfServers; i++) {
        nextServer = (nextServer + 1) % numOfServers;
        if (serversInfo[nextServer].first != "error")
            break;
        else if (i == numOfServers - 1)
            return -1;
    }
    
    return serverIdx;
}

int LoadBalancer::leastConnected() {
    vector<int> serversIdxList;
    for (int idx = 0; idx < serversInfo.size(); idx++) {
        if (serversInfo[idx].first != "error")
            serversIdxList.push_back(idx);
    }

    int serverIdx = -1;
    int minConnections = INT32_MAX;
    for (int i = 0; i < serversIdxList.size(); i++) {
        if (minConnections > serversTotalConnections[serversIdxList[i]]) {
            serverIdx = serversIdxList[i];
            minConnections = serversTotalConnections[serversIdxList[i]];
        }
    }

    return serverIdx;
}

void LoadBalancer::recvHttpRequest(struct context* ctx) {
    int max_bytes = sizeof(ctx->buffer);
    int httpRequestSize = recv(ctx->clientSocket, ctx->buffer, max_bytes, 0);
    
    if (httpRequestSize < 0) {
        handleError(ctx, "error receiving request");
        return;
    };

    struct pollfd* clientfd = getPoolfd(ctx->clientPoolfdIndex);
    ctx->bufferUseSize = httpRequestSize;
    // return a pointer with the position to the position of the searched char
    ctx->bufferEnd = (char*) memchr(ctx->buffer, '\n', ctx->bufferUseSize);
    if (ctx->bufferEnd) {
        ctx->state = SendingRequest; // change state
        ctx->serverIndex = getServerIndex();
        clientfd->events = POLLOUT;
    }
}

void LoadBalancer::sendHttpRequest(struct context* ctx) {
    if (!ctx->haveConnectionWithServer) {
        createServerConnection(ctx);
        if (!ctx->haveConnectionWithServer)
            return;
    }
    
    int max_bytes = ctx->bufferUseSize;
    int sentRequestSize = send(ctx->serverSocket, ctx->buffer, max_bytes, MSG_NOSIGNAL);
    
    if (sentRequestSize < 0) {
        handleError(ctx, "error sending request");
        return;
    }

    struct pollfd* serverfd = getPoolfd(ctx->serverPoolfdIndex);
    ctx->bufferUseSize = sentRequestSize;

    if (sentRequestSize == max_bytes) {
        ctx->state = ReceivingResponse;
        serverfd->events = POLLIN;
    }
}

void LoadBalancer::recvHttpResponse(struct context* ctx) {
    int max_bytes = ctx->bufferUseSize;
    int httpRequestSize = recv(ctx->serverSocket, ctx->buffer, max_bytes, 0);
    
    if (httpRequestSize < 0) {
        handleError(ctx, "error receiving response");
        return;
    }

    struct pollfd* serverfd = getPoolfd(ctx->serverPoolfdIndex);
    ctx->bufferUseSize = httpRequestSize;
    // return a pointer with the position to the position of the searched char
    ctx->bufferEnd = (char*) memchr(ctx->buffer, '\n', ctx->bufferUseSize);
    if (ctx->bufferEnd) {
        ctx->state = SendingResponse; // change state
    }
}

void LoadBalancer::sendHttpResponse(struct context* ctx) {
    int max_bytes = ctx->bufferUseSize;
    
    int sentRequestSize = send(ctx->clientSocket, ctx->buffer, max_bytes, MSG_NOSIGNAL);
    
    if (sentRequestSize < 0) {
        handleError(ctx, "error sending response");
        return;
    }

    struct pollfd* clientfd = getPoolfd(ctx->clientSocket);
    ctx->bufferUseSize = sentRequestSize;

    if (sentRequestSize == max_bytes) {
        ctx->state = Done; // change state
    }
}

void LoadBalancer::sendErrorResponse(struct context* ctx) {
    sendErrorMessage(ctx);
    ctx->state = Done;
}

void LoadBalancer::sendErrorMessage(struct context* ctx) {
    const string errorMessage = "HTTP/1.1 500 Internal Server Error\nContent-Type: text/plain\nContent-Length: 25\n\n500 Internal Server Error";
    const int errorMessageSize = (int) errorMessage.size();

    if (ctx->clientSocket < 0) {
        ctx->state = Done;
        return;
    }

    int sentRequestSize = send(ctx->clientSocket, errorMessage.c_str(), errorMessageSize, MSG_NOSIGNAL);
    
    if (sentRequestSize < 0) {
        ctx->state = Done;
    } else if (sentRequestSize == errorMessageSize) {
        ctx->state = Done;
    }
}

void LoadBalancer::removeServer(struct context* ctx) {
    if (ctx->serverIndex != -1) {
        serversErros[ctx->serverIndex]++;
        if (serversErros[ctx->serverIndex] == maxServerTries) {
            serversInfo[ctx->serverIndex].first = "error"; 
        }
    }
}

void LoadBalancer::handleError(struct context* ctx, const char* error) {
    if (debugMode)
        perror(error);

    if (ctx != NULL) {
        if (ctx->state == SendingRequest) {
            if (ctx->tries < maxTries) {
                removeServer(ctx);

                if (ctx->serverIndex != -1)
                    serversTotalConnections[ctx->serverIndex]--;
                ctx->serverIndex = getServerIndex(); // get new server in case of error
                
                ctx->haveConnectionWithServer = false;
                close(ctx->serverSocket);
                ctx->serverSocket = -1;
                ctx->tries++;
                return;
            }
        }

        sendErrorMessage(ctx);
        ctx->state = Error;
    }
}