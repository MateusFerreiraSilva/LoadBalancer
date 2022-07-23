#include "LoadBalancer.h"

LoadBalancer::LoadBalancer() {
    // alocate resources

    requestData = (char*) malloc(requestDataSize * sizeof(char));
    responseData = (char*) malloc(responseDataSize * sizeof(char));
}

void LoadBalancer::run() {
     // Creating socket file descriptor
    if ((masterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        perror("In socket");
        exit(EXIT_FAILURE);
    }

    int option = 1;
    if (setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0) {
        perror("Erro while setting master socket options");
		exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    memset(address.sin_zero, '\0', sizeof address.sin_zero); // unecessary?
    
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

    connectServers();

    printf("\n------------ load balancer running on port %d ------------\n", port);

    while(true) {
        int new_connection = acceptConnection();
        passRequest(new_connection);
        close(new_connection);
    }

}

void LoadBalancer::disconnectClient(int socket) {
    // printf("Disconnection , socket fd is %d , ip is : %s , port : %d \n", socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));
    close(socket);
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
    struct sockaddr_in clientAddress;
    int clientAddrlen = sizeof(clientAddress);
    int socket = accept(masterSocket, (struct sockaddr *)&clientAddress, (socklen_t *)&clientAddrlen);
    printf("\nNew connection , socket fd is %d , ip is : %s , port : %d \n", socket, inet_ntoa(clientAddress.sin_addr), ntohs(clientAddress.sin_port));
    if (socket < 0) {
        perror("Error acception connection");
    }

    return socket;
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