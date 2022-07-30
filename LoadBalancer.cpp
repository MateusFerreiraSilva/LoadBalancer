#include "LoadBalancer.h"

LoadBalancer::LoadBalancer() {
    // alocate resources

    // requestData = (char*) malloc(requestDataSize * sizeof(char));
    // responseData = (char*) malloc(responseDataSize * sizeof(char));

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        connections[i] = (struct context*)malloc(sizeof(struct context));
    } 
}

void LoadBalancer::setNonBlocking(int socket) {
    int flags = fcntl(socket, F_GETFD, 0); // get current flags
    if (fcntl(socket, F_SETFD, flags | O_NONBLOCK)) {
        handle_error("fcntl");
    }
}

void LoadBalancer::setReuseAddr(int socket) {
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &REUSEADDR, sizeof(REUSEADDR)) < 0) {
        perror("Erro while setting master socket options");
		exit(EXIT_FAILURE);
    }
}

void LoadBalancer::init() {
    // sigemptyset(&sigset);
    // sigaddset(&sigset, SIGTERM);
    // sigprocmask(SIG_SETMASK, &sigset, NULL);

    // int signal_fd = signalfd(-1, &sigset, 0);
    // pollfds[SIGNAL_FD].fd = signal_fd;
    // pollfds[SIGNAL_FD].events = POLLIN;

    startListen();
    pollfds[SERVER_FD].fd = masterSocket;
    pollfds[SERVER_FD].events = POLLIN;

    for (int i = FIRST_CONNECTION; i < POLLFDS; i++) {
        pollfds[i].fd = -1;
        pollfds[i].events = 0;
    }

    for (int i = 0; i < MAX_CONNECTIONS; i++) connections[i]->valid = false;    
    total_connections = 0;
}

void LoadBalancer::startListen() {
    if ((masterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        perror("In socket");
        exit(EXIT_FAILURE);
    }

    setNonBlocking(masterSocket);
    setReuseAddr(masterSocket);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(masterSocket, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("In bind");
        exit(EXIT_FAILURE);
    }

    if (listen(masterSocket, backlog) < 0)
    {
        perror("In listen");
        exit(EXIT_FAILURE);
    }
}

void LoadBalancer::run() {
    init();

    // connectServers();

    printf("\n------------ load balancer running on port %d ------------\n", port);

    while(true) {
        for (int i = 0; i < POLLFDS; i++) pollfds[i].revents = 0;

        if (poll(pollfds, POLLFDS, -1) < 0) { // third arg timeout, if -1 then infinite
            perror("poll");
        }

        acceptConnection();
        handleConnections();
        // int new_connection = acceptConnection();
        // passRequest(new_connection);
        // close(new_connection);
    }

}

void LoadBalancer::handleConnections() {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        int revents = pollfds[i + FIRST_CONNECTION].revents;
        if (revents & POLLERR) {
            handle_error("socket failure");
        } else if (revents) {
            struct context* connection = connections[i];
            short events_out = 0;
            int connection_completed = 0;
            if (handleConn(connection, revents, &events_out, &connection_completed)) {
                handle_error("handle_connection");
            }
            pollfds[i + FIRST_CONNECTION].events = events_out;
            /* If a connection was completed, free the context object. If
                * the number of connections was capped, the server is now free
                * to serve more clients, so add the server socket back to the
                * polling list. */
            if (connection_completed) {
                destroyConnection(connection);
                // connections[i] = NULL;
                pollfds[i + FIRST_CONNECTION].fd = -1;
                if (total_connections == MAX_CONNECTIONS) {
                    pollfds[SERVER_FD].fd = -pollfds[SERVER_FD].fd;
                }
            }
        }
    }
}

/*
 * TO DO change variable received by address
*/
int LoadBalancer::handleConn(struct context* ctx, short revents, short* events_out, int* connection_completed) {
    ssize_t result, max_bytes;

    /* POLLHUP means that the other end has closed the connection (hung up!). No
       need to continue. */
    if (revents & POLLHUP) {
        *connection_completed = 1;
        return 0;
    }

    switch (ctx->state) {
    case ReceivingRequest:
        max_bytes = sizeof(ctx->buf) - ctx->bytes - 1;
        result = read(ctx->fd, ctx->buf + ctx->bytes, max_bytes);
        if (result < 0) {
            return -1;
        }
        ctx->bytes += result;
        ctx->buf_end = (char*) memchr(ctx->buf, '\n', ctx->bytes);
        if (ctx->buf_end) {
            ctx->state = SendingRequest;
            ctx->bytes = 0;
            *events_out = POLLOUT;
        } else {
            *events_out = POLLIN;
            *connection_completed = 0;
            break;
        }
        // fallthrough
    case SendingRequest:
        max_bytes = strlen(ctx->buf) - ctx->bytes;
        /* Similarly as with reading, writing may not write all the bytes at
         * once, and we may need to wait for the socket to become readable
         * again. */
        result = write(ctx->fd, ctx->buf + ctx->bytes, max_bytes);
        if (result < 0) {
            return -1;
        }
        ctx->bytes += result;
        if (result == max_bytes) {
            ctx->state = Done;
        } else {
            *events_out = POLLOUT;
            *connection_completed = 0;
            break;
        }
        // fallthrough
        case Done:
            *events_out = 0;
            *connection_completed = 1;
        };

    return 0;
}

void LoadBalancer::destroyConnection(struct context* ctx)
{
    ctx->valid = false;
    close(ctx->fd);
    // free(ctx);
}

void LoadBalancer::disconnectClient(int socket) {
    // printf("Disconnection , socket fd is %d , ip is : %s , port : %d \n", socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));
    close(socket);
    total_connections--;
}

void LoadBalancer::connectServers() {
    for (int i = 0; i < servers.size(); i++) {
        const char* serverIpAddress = servers[i].first.c_str();
        int serverPortNumber = servers[i].second;

        SocketAddress serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(serverPortNumber);

        // Convert IPv4 and IPv6 addresses from text to binary form
        if (inet_pton(AF_INET, serverIpAddress, &serverAddr.sin_addr) <= 0) {
            printf("\nInvalid address/ Address not supported \n");
            
            return;
        }

        serversAddresses.push_back(serverAddr);
        createServerConnection(serverAddr);
    }

    printf("Number of connected servers: %d\n\n", (int) serversSockets.size());

}

void LoadBalancer::createServerConnection(SocketAddress serverAddr) {
    int serverSocket;
    if ((serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("In socket");
        exit(EXIT_FAILURE);
    }
    
    setRecvTimeout(serverSocket);

    if ((connect(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr))) < 0) {
        printf("\nConnection Failed \n");
        return;
    }

    serversSockets.push_back(serverSocket);
}

int LoadBalancer::acceptConnection() {
     if (pollfds[SERVER_FD].revents & POLLERR) {
        handle_error("server failure");
    } else if (pollfds[SERVER_FD].revents & POLLIN) {
        struct sockaddr_in clientAddress;
        int clientAddrlen = sizeof(clientAddress);
        int socket = accept(masterSocket, (struct sockaddr *)&clientAddress, (socklen_t *)&clientAddrlen);
        // printf("\nNew connection , socket fd is %d , ip is : %s , port : %d \n", socket, inet_ntoa(clientAddress.sin_addr), ntohs(clientAddress.sin_port));
        if (socket < 0) {
            perror("Error acception connection");
        }

        setNonBlocking(socket);

        for (int i = 0; i < MAX_CONNECTIONS; i++) { 
            if (!connections[i]->valid) {
                short events_out = 0;
                struct context* connection = createConnection(i, socket, &events_out);
                if (!connection->valid) {
                    handle_error("create_connection");
                }

                pollfds[FIRST_CONNECTION + i].fd = socket;
                pollfds[FIRST_CONNECTION + i].events = events_out;
                total_connections++;
                break;
            }
        }

        return socket;
    }
    return 0; // TO DO fix this
}

struct context* LoadBalancer::createConnection(int index, int socket, short* events_out) {
    connections[index]->fd = socket;
    connections[index]->state = ReceivingRequest;
    memset(connections[index]->buf, 0, sizeof(connections[index]->buf));
    connections[index]->bytes = 0;
    connections[index]->buf_end = NULL;
    *events_out = POLLIN;
    connections[index]->valid = true;

    return connections[index];
}


void LoadBalancer::setRecvTimeout(int socket) {
    struct timeval tv;
    tv.tv_sec = secondsToTimeout;
    tv.tv_usec = 0;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
}

int LoadBalancer::getServerSocket() {
    // round robin
    int serverIdx = nextServer;
    nextServer = (nextServer + 1) % (int) serversSockets.size();
    
    return serversSockets[serverIdx];
}

void LoadBalancer::passRequest(int clientSocket) {
    // printf("Processing Http Request To Socket: %d\n", socket);

    int serverSocket = getServerSocket();

    setRecvTimeout(clientSocket);
    int sizeToBeSent = recvHttpRequest(clientSocket);
    sendHttpRequest(serverSocket, sizeToBeSent);
    sizeToBeSent = recvHttpResponse(serverSocket);
    sendHttpResponse(clientSocket, sizeToBeSent);

    // try {
    // }
    // catch(const std::runtime_error& re) {
    //     handleError(socket);
    //     cerr << "Runtime error: " << re.what() << std::endl;
    // }
    // catch(const std::exception& ex) {
    //     handleError(socket);
    //     cerr << "Error occurred: " << ex.what() << std::endl;
    // }
    // catch(...)
    // {
    //     handleError(socket);
    // }


    // printf("Request was completed\n");
}

int LoadBalancer::recvHttpRequest(int clientSocket) {
    int httpRequestSize = recv(clientSocket , requestData, requestDataSize, 0); // timeout of 1 second
    if (httpRequestSize > 0) {
        // printf("%s\n", requestData);
        string doNothing = "";
    } else if (httpRequestSize <= 0) {
        disconnectClient(clientSocket);
    }

    return httpRequestSize;
}

void LoadBalancer::sendHttpRequest(int serverSocket, int sizeToBeSent) {
    int sentRequestSize = send(serverSocket, requestData, sizeToBeSent, MSG_NOSIGNAL);
}

int LoadBalancer::recvHttpResponse(int serverSocket) {
    int httpResponseSize = recv(serverSocket , responseData, responseDataSize, 0); // timeout of 1 second
    if (httpResponseSize > 0) {
        printf("%s\n", responseData);
    }

    return httpResponseSize;
}

void LoadBalancer::sendHttpResponse(int serverSocket, int sizeToBeSent) {
    int sentRequestSize = send(serverSocket, responseData, sizeToBeSent, MSG_NOSIGNAL);
}

/*
 * Temp
*/
void LoadBalancer::handle_error(const char* error) {
    perror(error);
    exit(1);
}