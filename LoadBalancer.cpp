#include "LoadBalancer.h"

LoadBalancer::LoadBalancer() {
    contexts.resize(MAX_CONTEXTS);
}

void LoadBalancer::setNonBlocking(int socket) {
    int flags = fcntl(socket, F_GETFD, 0); // get current flags
    if (fcntl(socket, F_SETFD, flags | O_NONBLOCK))
        handle_error("fcntl");
}

void LoadBalancer::setReuseAddr(int socket) {
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &REUSEADDR, sizeof(REUSEADDR)) < 0)
        handle_error("Erro while setting master socket options");
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
            perror("poll");
        }

        acceptConnections();
        handleConnections();
    }
}

void LoadBalancer::init() {
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
    if ((masterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) handle_error("In socket");

    setNonBlocking(masterSocket);
    setReuseAddr(masterSocket);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(masterSocket, (struct sockaddr *)&address, sizeof(address)) < 0) handle_error("In bind");
    if (listen(masterSocket, backlog) < 0) handle_error("In listen");
}

void LoadBalancer::connectServers() {
    for (int i = 0; i < serversInfo.size(); i++) {
        const char* serverIpAddress = serversInfo[i].first.c_str();
        int serverPortNumber = serversInfo[i].second;

        struct sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(serverPortNumber);

        // Convert IPv4 and IPv6 addresses from text to binary form
        if (inet_pton(AF_INET, serverIpAddress, &serverAddr.sin_addr) <= 0)
            handle_error("\nInvalid address/ Address not supported \n");

        serversAddresses.push_back(serverAddr);
    }
}

void LoadBalancer::acceptConnections() {
    if (pollfds[SERVER_FD].revents & POLLERR)
        handle_error("server failure");
    
    if (pollfds[SERVER_FD].revents & POLLIN) { // check if have activity on the laodbalancer listener
        struct sockaddr_in clientAddress;
        int clientAddrlen = sizeof(clientAddress);
        int clientSocket = accept(masterSocket, (struct sockaddr *)&clientAddress, (socklen_t *)&clientAddrlen);
        // printf("\nNew connection , socket fd is %d , ip is : %s , port : %d \n", socket, inet_ntoa(clientAddress.sin_addr), ntohs(clientAddress.sin_port));
        if (clientSocket < 0) handle_error("Error acception connection");

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
        if (ctx->inUse == false) continue;

        const int fdsSize = 2;
        struct pollfd* fds[fdsSize];
        fds[0] = getPoolfd(ctx->clientPoolfdIndex);
        fds[1] = getPoolfd(ctx->serverPoolfdIndex);

        for (int j = 0; j < fdsSize; j++) {
            if (fds[j] == NULL) continue; // if not valid fd, go to next

            int revents = fds[j]->revents;
            if (revents & POLLERR) handle_error("socket failure");
            
            if (revents) {
                processAction(ctx);
            }
        }
    }
}

void LoadBalancer::processAction(struct context* ctx) {
    switch (ctx->state) {
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
    struct sockaddr_in serverAddr = serversAddresses[ctx->serverIndex]; // TO DO function to access it
    int serverSocket;
    if ((serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("In socket");
        exit(EXIT_FAILURE);
    }
    
    setRecvTimeout(serverSocket);

    if ((connect(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr))) < 0)
        handle_error("Connection Failed");

    // TO DO see how to configure a connect to be async

    setNonBlocking(serverSocket);
    addServerToContext(ctx, serverSocket);
}

void LoadBalancer::addServerToContext(struct context* ctx, int serverSocket) {
    int serverPoolfdIndex = getFreePoolfdIndex();
    ctx->serverSocket = serverSocket;
    ctx->serverPoolfdIndex = serverPoolfdIndex;
    pollfds[serverPoolfdIndex].fd = serverSocket;
    pollfds[serverPoolfdIndex].events = POLLOUT;
}

void LoadBalancer::setRecvTimeout(int socket) {
    struct timeval tv;
    tv.tv_sec = secondsToTimeout;
    tv.tv_usec = 0;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
}

int LoadBalancer::getServerIndex() {
    // round robin
    int serverIdx = nextServer;
    nextServer = (nextServer + 1) % (int) serversAddresses.size();
    
    return serverIdx;
}

void LoadBalancer::recvHttpRequest(struct context* ctx) {
    int max_bytes = sizeof(ctx->buffer);
    int httpRequestSize = recv(ctx->clientSocket, ctx->buffer, max_bytes, 0);
    
    if (httpRequestSize < 0) return;

    struct pollfd* clientfd = getPoolfd(ctx->clientPoolfdIndex);
    ctx->bufferUseSize = httpRequestSize;
    // return a pointer with the position to the position of the searched char
    ctx->bufferEnd = (char*) memchr(ctx->buffer, '\n', ctx->bufferUseSize);
    if (ctx->bufferEnd) {
        ctx->state = SendingRequest; // change state
        ctx->serverIndex = getServerIndex();
        clientfd->events = POLLOUT;
    } else {
        clientfd->events = POLLIN;
    }
}

void LoadBalancer::sendHttpRequest(struct context* ctx) {
    createServerConnection(ctx);
    
    int max_bytes = ctx->bufferUseSize;
    
    int sentRequestSize = send(ctx->serverSocket, ctx->buffer, max_bytes, MSG_NOSIGNAL);
    
    if (sentRequestSize < 0) return;

    struct pollfd* serverfd = getPoolfd(ctx->serverPoolfdIndex);

    ctx->bufferUseSize = sentRequestSize;
    if (sentRequestSize == max_bytes) {
        ctx->state = ReceivingResponse;
        serverfd->events = POLLIN;
    } else {
        serverfd->events = POLLOUT;
    }
}

void LoadBalancer::recvHttpResponse(struct context* ctx) {
    int max_bytes = ctx->bufferUseSize;
    int httpRequestSize = recv(ctx->serverSocket, ctx->buffer, max_bytes, 0);
    
    if (httpRequestSize < 0) return;

    struct pollfd* serverfd = getPoolfd(ctx->serverPoolfdIndex);
    ctx->bufferUseSize = httpRequestSize;
    // return a pointer with the position to the position of the searched char
    ctx->bufferEnd = (char*) memchr(ctx->buffer, '\n', ctx->bufferUseSize);
    if (ctx->bufferEnd) {
        ctx->state = SendingResponse; // change state
    } else {
        serverfd->events = POLLIN;
    }
}

void LoadBalancer::sendHttpResponse(struct context* ctx) {
    int max_bytes = ctx->bufferUseSize;
    
    int sentRequestSize = send(ctx->clientSocket, ctx->buffer, max_bytes, MSG_NOSIGNAL);
    
    if (sentRequestSize < 0) return;

    struct pollfd* clientfd = getPoolfd(ctx->clientSocket);

    ctx->bufferUseSize = sentRequestSize;
    if (sentRequestSize == max_bytes) {
        ctx->state = Done; // change state
    } else {
        clientfd->events = POLLOUT;
    }
}

/*
 * Temp
*/
void LoadBalancer::handle_error(const char* error) {
    perror(error);
    exit(1);
}