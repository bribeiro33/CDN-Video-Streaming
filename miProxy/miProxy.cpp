#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <queue>
#include <cstring>
#include <unordered_map>
#include <string>
#include <utility>
#include <regex>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <netdb.h>
#include <algorithm>
#include <sstream>
#include "../starter_files/DNSHeader.h"
#include "../starter_files/DNSQuestion.h"
#include "../starter_files/DNSRecord.h"
#include "miProxy.hpp"

#define BACKLOG 10
#define PACKET_LEN 1000
#define SERVER_PORT 80

#define DEBUG 0

// steps as I understand them:
// 1. browser client requests video stream from proxy
// 2. proxy forwards request to server
// 3. server responds to proxy with manifest file
// 3.a proxy parses XML to retrieve available bitrates
// 3.b proxy requests big_buck_bunny_nolist f4m file from server
// 3.c proxy fowards the _nolist to the browser 
// 4. for following browser requests to proxy, proxy selects the best bitrate, modifies the request header, and forwards it to the server
// 5. server forwards it to proxy, proxy forwards it to browser 

// recieve from browser client --> initial req(i think is req for f4m), chunk requests
// send to browser client --> no_list f4m, chunks w/ modified bitrates
// send to server --> f4m req, chunk req
// recieve from server --> XML bitrates, no_list f4m, data chunks

std::unordered_map<int, int> serverToBrowser;
std::unordered_map<int, int> browserToServer;
std::unordered_map<std::string, float> IPToThroughput;
// Map to keep track of socket FDs and their corresponding IP addresses
std::unordered_map<int, std::string> socketToIP;
// map to keep track of modified data for clientIPs in order to log them later
std::unordered_map<std::string, std::pair<std::string, double>> ipDataMap;
// queue for requests
std::unordered_map<std::string, std::queue<std::string>> requestsToServer;
std::unordered_map<std::string, double> previousBitrate;
int dns_query_id = 0; //used for DNSHeader.ID

int main(int argc, char **argv){
    std::fstream log;
    if (argc < 6){
        perror("Error: missing or extra arguments\n");
        exit(1);
    }
    if (strcmp(argv[1], "--nodns") == 0){
        if (argc != 6){
            perror("Error: missing or extra arguments\n");
            exit(1);
        }
        int listen_port = atoi(argv[2]);
        char *www_ip = argv[3];
        float alpha = atof(argv[4]);
        log.open(argv[5], std::fstream::out);
        if (!log.is_open()){
            perror("failed to open log");
            exit(1);
        }
        runProxy(listen_port, www_ip, alpha, log);
    }
    else if (strcmp(argv[1], "--dns") == 0){
        if (argc != 7){
            perror("Error: missing or extra arguments\n");
            exit(1);
        }
        int listen_port = atoi(argv[2]);
        char *dns_ip = argv[3];
        int dns_port = atoi(argv[4]);
        float alpha = atof(argv[5]);
        log.open(argv[6], std::fstream::out);
        if (!log.is_open()){
            perror("failed to open log");
            exit(1);
        }
        int dns_fd = connectToServer(dns_ip, dns_port);
        send_dns_header(dns_fd);
        send_dns_question(dns_fd);
        DNSHeader header = receive_dns_header(dns_fd);
        DNSRecord record = receive_dns_record(dns_fd);
        close(dns_fd);
        runProxy(listen_port, record.RDATA, alpha, log);
    }
    else{
        perror("Error: invalid flag\n");
        exit(1);
    }
}

void runProxy(int listen_port, char* server_ip, float alpha, std::fstream& log){
    if(DEBUG) std::cout << "runProxy" << std::endl;
    int activity;
    std::vector<int> sockets;
    struct sockaddr_in addr;
    int listen_fd = setupListeningSocket(listen_port, &addr);

    std::vector<int> availableBitrates;

    // initialize set of active sockets(file descriptors)
    fd_set readfds;
    // FD_ZERO(&readfds);
    // FD_SET(listen_fd, &readfds);
    // int max_fd = listen_fd;
    
    // wait for new connections or data from existing connections
    while (true){
        FD_ZERO(&readfds);
        FD_SET(listen_fd, &readfds);
        for(const auto &sock : sockets) {
            FD_SET(sock, &readfds);
        }
        // keep og set of active sockets intact and use a temp set for select
        // fd_set temp_set = readfds;
        activity = select(FD_SETSIZE, &readfds, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR)){
            perror("Error: select failed\n");
            // close(listen_fd);
            // exit(1);
        }

        // Check each socket (including listening socket) for read activity
        if(FD_ISSET(listen_fd, &readfds)) {
            handleNewClient(listen_fd, server_ip, SERVER_PORT, sockets);
        }
        for (auto &sock : sockets) {
            if (FD_ISSET(sock, &readfds)) {
                int success = 0;
                // if (i == listen_fd) {
                //     // accept new connection
                //     handleNewClient(listen_fd, max_fd, server_ip, SERVER_PORT, readfds);
                // }
                if (isClientSocket(sock)){
                    success = handleClientRequest(sock, availableBitrates, log, sockets);
                }
                else if (isServerSocket(sock)){
                    success = handleServerResponse(sock, availableBitrates, alpha, log, sockets);
                }
            }
        }
    } 

    // Clean up before shutting down
    log.close();
    close(listen_fd);
}

int setupListeningSocket(int port, struct sockaddr_in *addr){
    if(DEBUG) std::cout << "setupListeningSocket" << std::endl;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0); 
    if (sockfd < 0){
        perror("Error: socket creation failed\n");
        exit(1);
    }

    int yes = 1;
    int success = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if(success < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;
    addr->sin_port = htons(port);
    
    if (bind(sockfd, (struct sockaddr *) addr, sizeof(*addr)) < 0){
        perror("Error: bind failed\n");
        close(sockfd);
        exit(1);
    }
    if (listen(sockfd, BACKLOG) < 0){
        perror("Error: listen failed\n");
        close(sockfd);
        exit(1);
    }
    return sockfd;
}

// Function to handle a new client connection
void handleNewClient(int listen_fd, const char* server_ip, int server_port, std::vector<int> &sockets) {
    if(DEBUG) std::cout << "handleNewClient" << std::endl;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_sock = accept(listen_fd, (struct sockaddr *) &client_addr, &client_addr_len);
    if (client_sock < 0){
        perror("Error: accept failed\n");
        exit(1);
    }

    // connect to the server on behalf of the client
    int server_sock = connectToServer(server_ip, server_port);
    if (server_sock < 0) {
        close(client_sock);
        perror("Error: failed to connect to server\n");
        return;
    }

    // keep track of the client and server ips by their socket
    socketToIP[client_sock] = std::string(inet_ntoa(client_addr.sin_addr));
    socketToIP[server_sock] = std::string(server_ip);

    // add the new client socket to the read file descriptor set
    sockets.push_back(client_sock);
    // FD_SET(client_sock, &readfds);
    // add the new server socket to the read file descriptor set
    sockets.push_back(server_sock);
    // FD_SET(server_sock, &readfds);
    // update the max file descriptor
    // max_fd = std::max(client_sock, server_sock);

    // update the mappings
    browserToServer[client_sock] = server_sock;
    serverToBrowser[server_sock] = client_sock;
}

// util func to create a connection to the server
int connectToServer(const char* server_ip, int server_port) {
    if(DEBUG) std::cout << "connectToServer" << std::endl;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Failed to create socket for server connection\n");
        return -1;
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    struct hostent *host = gethostbyname(server_ip);
    if(host == nullptr) {
        perror("Unknown host");
        return -1;
    }
    memcpy(&(serverAddr.sin_addr), host->h_addr, host->h_length);
    serverAddr.sin_port = htons(server_port);
    // inet_pton(AF_INET, server_ip, &serverAddr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Failed to connect to server\n");
        close(sock);
        return -1;
    }

    return sock;
}

// recieve from browser client --> initial req for f4m, chunk GET requests
// send to server --> f4m req, chunk req
int handleClientRequest(int client_fd, std::vector<int>& availableBitrates, std::fstream& log, std::vector<int> &sockets) {
    if(DEBUG) std::cout << "handleClientRequest" << std::endl;
    int server_fd = browserToServer[client_fd];

    // buffer should be the size given in the header under content size

    char buffer[1024]; // for storing recieved data
    memset(buffer, 0, sizeof(buffer)); 
    std::string request;
    // load in header
    int bytesRead;
    do {
        bytesRead = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytesRead <= 0){
            perror("Failed to read from client or connection closed");
            // remove sockets from list of active connections
            size_t idx = sockets.size();
            for(size_t i = 0; i < sockets.size(); ++i) {
                if(sockets[i] == client_fd) {
                    idx = i;
                    break;
                }
            }
            if(idx != sockets.size()) sockets.erase(sockets.begin()+idx);
            idx = sockets.size();
            for(size_t i = 0; i < sockets.size(); ++i) {
                if(sockets[i] == server_fd) {
                    idx = i;
                    break;
                }
            }
            if(idx != sockets.size()) sockets.erase(sockets.begin()+idx);
            // close client and serv sockets
            close(client_fd);
            close(server_fd);
            // remove them from their maps
            browserToServer.erase(client_fd);
            serverToBrowser.erase(server_fd);
            return -1;
        }
        request.append(buffer, bytesRead);
    } while(request.find("\r\n\r\n") == std::string::npos);

    // No Body in Request
    // load in body
    // std::string request = buffer;
    // int new_bytes = loadAllData(server_fd, request, buffer, sizeof(buffer));
    // if (new_bytes < 0){
    //     perror("Error: failed to load all data from server response");
    //     return -1;
    // }
    // request = std::string(buffer, bytesRead);
    if(DEBUG) std::cout << "request: \n\n" << request << std::endl;
    // separate requested
    std::string line1 = request.substr(0, request.find("\n"));
    std::string requested = line1.substr(line1.find(" ")+1, line1.rfind(" ")-line1.find(" ")-1);
    requestsToServer[socketToIP[client_fd]].push(requested);
    if(requested.find("big_buck_bunny.f4m") != std::string::npos) {
        std::string nolist_request = request.substr(0, request.find(".f4m")) + 
                                        "_nolist" + request.substr(request.find(".f4m"));
        // Send original request first
        if(send(server_fd, request.c_str(), request.length(), 0) < 0) {
            perror("Error: failed sending client request to server");
            return -1;
        }
        requestsToServer[socketToIP[client_fd]].push(requested.substr(0, requested.find(".f4m")) + "_nolist" + requested.substr(requested.find(".f4m")));
        // Then change original request to nolist_request
        request = nolist_request;
    }
    // requesting video chunk, not manifest file
    else if (requested.find("Seg") != std::string::npos && requested.find("-Frag") != std::string::npos) {
        std::string client_ip = socketToIP[client_fd];
        double currThroughput = getCurrentThroughput(client_ip, availableBitrates);
        request = modifyChunkRequestURI(request, currThroughput, availableBitrates, client_ip);
        line1 = request.substr(0, request.find("\n"));
        requested = line1.substr(line1.find(" ")+1, line1.rfind(" ")-line1.find(" ")-1);
        if(DEBUG) std::cout << "New line1: " << line1 << std::endl;
        // for logging
        // std::string chunkname = request.substr(request.find_last_of('/') + 1);
        // size_t nonNumericPos = chunkname.find_first_not_of("0123456789");
        size_t bitrateStart = requested.find('/', requested.find('/')+1)+1;
        size_t segStart = requested.find("Seg");
        // double bitrate = std::stod(chunkname.substr(0, nonNumericPos));
        double bitrate = std::stod(requested.substr(bitrateStart, segStart - bitrateStart));
        updateIPData(client_ip, requested, bitrate);
    }

    // Forward the possibly modified request to the server
    if (send(server_fd, request.c_str(), request.length(), 0) < 0) {
        perror("Error: failed to send client request to server");
        return -1;
    }
    return 0;
}

// send to browser client --> no_list f4m, chunks w/ modified bitrates
// recieve from server --> XML bitrates, no_list f4m, data chunks
int handleServerResponse(int server_fd, std::vector<int>& availableBitrates, float alpha, std::fstream& log, std::vector<int> &sockets)  {
    if(DEBUG) std::cout << "handleServerResponse" << std::endl;
    int client_fd = serverToBrowser[server_fd];

    // buffer should be the size given in the header under content size

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    std::chrono::time_point<std::chrono::high_resolution_clock> start = std::chrono::high_resolution_clock::now();

    // receive header
    std::string response;
    int bytesRead;
    do {
        bytesRead = recv(server_fd, buffer, sizeof(buffer), 0);
        if (bytesRead <= 0){
            perror("Failed to read from client or connection closed");
            // remove sockets from list of active connections
            size_t idx = sockets.size();
            for(size_t i = 0; i < sockets.size(); ++i) {
                if(sockets[i] == client_fd) {
                    idx = i;
                    break;
                }
            }
            if(idx != sockets.size()) sockets.erase(sockets.begin()+idx);
            idx = sockets.size();
            for(size_t i = 0; i < sockets.size(); ++i) {
                if(sockets[i] == server_fd) {
                    idx = i;
                    break;
                }
            }
            if(idx != sockets.size()) sockets.erase(sockets.begin()+idx);
            // close client and serv sockets
            close(client_fd);
            close(server_fd);
            // remove them from their maps
            browserToServer.erase(client_fd);
            serverToBrowser.erase(server_fd);
            return -1;
        }
        response.append(buffer, bytesRead);
    } while(response.find("\r\n\r\n") == std::string::npos);
    int new_bytes = loadAllData(server_fd, response, buffer, sizeof(buffer));
    if (new_bytes < 0){
        perror("Error: failed to load all data from server response");
        return -1;
    }
    // response = std::string(buffer, bytesRead);
    std::chrono::time_point<std::chrono::high_resolution_clock> end = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000000.0;
    
    // Based on the request type from the queue, handle the response
    std::string requested = requestsToServer[socketToIP[client_fd]].front();
    requestsToServer[socketToIP[client_fd]].pop();

    if (requested.find("_nolist.f4m") != std::string::npos){
        // send _nolist.f4m to client
        if (send(client_fd, response.c_str(), response.size(), 0) < 0) {
            perror("Failed to send response to client");
        }
    }
    // if receive .f4m, request _nolist from server
    else if (requested.find(".f4m") != std::string::npos){
        // if(DEBUG) std::cout << "Response:\n\n" << response << std::endl;
        // get bitrates from f4m file
        getBitrates(response, availableBitrates);
        // send no list request to server TODO COULD VERY WELL BE WRONG
        // std::string request("GET big_buck_bunny_nolist.f4m HTTP/1.1\r\nHost: ");
        // // std::string request = response.substr(0,response.find(".f4m")) + "_nolist" + response.substr(response.find(".f4m"));
        // // might need to be a loop
        // if (send(server_fd, request.c_str(), request.size(), 0) < 0) {
        //     perror("Failed to send no list request to server");
        // }
    }
    else if(requested.find("Seg") != std::string::npos && requested.find("-Frag") != 0) {
        // if receive chunk, calc throughput and forward to client
        std::string browser_ip = socketToIP[client_fd];
        std::string server_ip = socketToIP[server_fd];
        std::pair<double,double> throughputs = calculateThroughput( duration, bytesRead, alpha, browser_ip, availableBitrates);
        double newThroughput = throughputs.first;
        double avgThroughput = throughputs.second;
        if(DEBUG) std::cout << "Calculated Throughput" << std::endl;
        // log the data
        std::pair<std::string,double> data = ipDataMap[browser_ip];
        std::string chunkname = data.first;
        double bitrate = data.second;
        previousBitrate[socketToIP[client_fd]] = bitrate;
        log << browser_ip << " " << chunkname << " " << server_ip << " " 
            << duration << " " << newThroughput << " " << avgThroughput << " " << bitrate << std::endl;

        if (send(client_fd, response.c_str(), response.size(), 0) < 0) {
            perror("Failed to send response to client");
            return -1;
        }
    }
    else {
        if (send(client_fd, response.c_str(), response.size(), 0) < 0) {
            perror("Failed to send response to client");
        }
    }
    return 0;
}

int loadAllData(int recv_fd, std::string &response, char* buffer, int buffer_size){
    if(DEBUG) std::cout << "loadAllData" << std::endl;
    int bytesRead = 0;
    int totalBytes = 0;
    int contentLength = findContentLength(response);
    if (contentLength == -1) {
        perror("Error: failed to find content length in server response");
        return -1;
    }
    int headerLength = findHeaderLength(response);
    // receive body, loop continues until all content is received (bytes read so far - header length < content length)
    while (response.size() - headerLength < contentLength) {
        bytesRead = recv(recv_fd, buffer, std::min(buffer_size, static_cast<int>(contentLength + headerLength - response.size())), 0);
        totalBytes += bytesRead;
        response.append(buffer, bytesRead);
    }
    return totalBytes;
}

// finds the header length of the HTTP header
int findHeaderLength(const std::string& s) {
    if(DEBUG) std::cout << "findHeaderLength" << std::endl;
    // find header termination sequence
    size_t found = s.find("\r\n\r\n");
    // if found, header len is everything up to and including this termination sequence (+4)
    // return -1 if not found
    return found != std::string::npos ? static_cast<int>(found + 4) : -1;
}

// finds the content length from the HTTP header
int findContentLength(const std::string& s) {
    if(DEBUG) std::cout << "findContentLength" << std::endl;
    size_t start = s.find("Content-Length: ");
    if (start == std::string::npos) return -1;

    // move to the end of "Content-Length: "
    start += 16;
    // end of http header
    size_t end = s.find("\r\n", start);
    if (end == std::string::npos) return -1;
    
    // parse the integer between content-len and the end seq
    std::string sub = s.substr(start, end - start);
    return atoi(sub.c_str());
}

std::string modifyChunkRequestURI(  const std::string& request, double currThroughput,
                                    const std::vector<int>& availableBitrates, std::string &client_ip) {
    if(DEBUG) std::cout << "modifyChunkRequestURI" << std::endl;
    // Find the highest bitrate that the connection can support
    int supportedBitrate;
    if(previousBitrate.count(client_ip) == 0) {
        supportedBitrate = availableBitrates[0];
    }
    else {
        supportedBitrate = previousBitrate[client_ip];
    }
    for (int bitrate : availableBitrates) {
        if (currThroughput >= 1.5 * bitrate) {
            supportedBitrate = bitrate;
        } else {
            break; // The bitrates are sorted, so we can break once we find one that's too high
        }
    }
    size_t bitrateStart = request.find('/', request.find('/')+1)+1;
    size_t segStart = request.find("Seg");
    if (bitrateStart == std::string::npos || segStart == std::string::npos) {
        perror("Error: not a valid URI chunk"); 
        return "";
    }
    // extract the part of the URI before and after the bitrate specification
    std::string uriPrefix = request.substr(0, bitrateStart);
    std::string uriSuffix = request.substr(segStart);
    // put together new uri with new bitrate
    std::string modifiedRequest = uriPrefix + std::to_string(supportedBitrate) + uriSuffix;
    if(DEBUG) std::cout << "Modified Request:\n\n" << modifiedRequest << std::endl;
    return modifiedRequest;
}

// extracts segment and fragment numbers from the request URI
std::pair<int, int> findSegAndFragNums(const std::string& s) {
    if(DEBUG) std::cout << "findSegAndFragNums" << std::endl;
    size_t segPos = s.find("Seg");
    size_t fragPos = s.find("Frag");
    // if either don't exist, return -1s
    if (segPos == std::string::npos || fragPos == std::string::npos) return {-1, -1};
    
    // extract seg num
    segPos += 3; // move past seg
    size_t segEnd = s.find('-', segPos);
    if (segEnd == std::string::npos) return {-1,-1};
    int seg = std::stoi(s.substr(segPos, segEnd - segPos));
    
    // extract frag num
    fragPos += 4; 
    size_t fragEnd = s.find_first_not_of("0123456789", fragPos); // find the first non-digit character
    int frag = std::stoi(s.substr(fragPos, fragEnd - fragPos));
    
    return {seg, frag};
}

// Parse the f4m file to get available bitrates
// example line: <media bitrate="1500" width="640" height="360" url="chunklist_b1500000.m3u8"/>
void getBitrates(const std::string& response, std::vector<int>& bitrates) {
    if(DEBUG) std::cout << "getBitrates" << std::endl;
    std::istringstream iss(response);
    std::string line;
    while (std::getline(iss, line, '<')) {
        // look for lines with both <media and bitrate=
        // might need to be more exact w/ bitrate following media but given html structure, this should be fine
        if (line.find("media") != std::string::npos && line.find("bitrate=") != std::string::npos) {
            size_t start = line.find("bitrate=") + 8; // 8 = bitrate+ num chars
            size_t end = line.find('"', start + 1); // search for closing quote after start quote (+1)
            if (end != std::string::npos) { // if end was found
                std::string bitrateStr = line.substr(start + 1, end - start - 1); // extract the bitrate str val
                int bitrate = std::stoi(bitrateStr); // convert to int
                bitrates.push_back(bitrate);
            }
        }
    }
    sort(bitrates.begin(), bitrates.end());
}

std::pair<double, double> calculateThroughput(  double duration, int chunkSize, double alpha,
                                                std::string &client_ip, std::vector<int> &availableBitrates) {
    if(DEBUG) std::cout << "calculateThroughput" << std::endl;
    double currThroughput = getCurrentThroughput(client_ip, availableBitrates);
    // calc Kbps throughput
    double newThroughput = (chunkSize * 8.0) / (duration * 1000.0);
    // calc EWMA thorughput
    double avgThroughput = alpha * newThroughput + (1 - alpha) * currThroughput;
    // set to new throughput to become the current throughput
    IPToThroughput[client_ip] = avgThroughput;

    return std::make_pair(newThroughput, avgThroughput);
}

bool isClientSocket(int fd) {
    return browserToServer.find(fd) != browserToServer.end();
}

bool isServerSocket(int fd) {
    return serverToBrowser.find(fd) != serverToBrowser.end();
}

double getCurrentThroughput(std::string &client_ip, std::vector<int> &availableBitrates){
    if(DEBUG) std::cout << "getCurrentThroughput" << std::endl;
    // if client_ip isn't already in the map, initialize its throughput to the lowest available bitrate
    if (IPToThroughput.count(client_ip) == 0){
        double lowestBitrate = availableBitrates[0];
        IPToThroughput[client_ip] = lowestBitrate;
    }

    return IPToThroughput[client_ip];
}

// int findNewMaxFd(fd_set& fds, int listen_fd) {
//     int new_max = listen_fd;
//     for (int i = listen_fd; i < FD_SETSIZE; ++i) {
//         if (FD_ISSET(i, &fds) && i > new_max) {
//             new_max = i;
//         }
//     }
//     return new_max;
// }

void send_dns_header(int connectionfd){
    DNSHeader header;
    header.ID = dns_query_id;
    dns_query_id++;
    header.QR = 0;
    header.OPCODE = 0;
    header.AA = 0;
    header.TC = 0;
    header.RD = 0;
    header.RA = 0;
    header.Z = 0;
    header.RCODE = 0;
    header.QDCOUNT = 1;
    header.ANCOUNT = 1;
    header.NSCOUNT = 0;
    header.ARCOUNT = 0;
    std::string dns_header = header.encode(header);
    int header_size = dns_header.size();
    //NOTE: may actually be htonl for the conversion. If things are not working try that
    int size = htonl(header_size);
    ssize_t sent = 0;
    ssize_t sval;
    do {
        sval = send(connectionfd, &size + sent, 4 - sent, 0);
        if (sval == -1) {
            perror("Error reading stream message");
            exit(1);
        }
        sent += sval;
    } while(sval > 0 && sent < 4);
    sent = 0;
    char message[1000];
    std::memcpy(message, dns_header.c_str(), dns_header.size());
    do {
        sval = send(connectionfd, message + sent, header_size - sent, 0);
        if (sval == -1) {
            perror("Error reading stream message");
            exit(1);
        }
        sent += sval;
    } while(sval > 0 && sent < size);
}

void send_dns_question(int connectionfd){
    DNSQuestion question;
    std::string query = "video.cse.umich.edu";
    std::memcpy(question.QNAME, query.c_str(), query.size());
    question.QTYPE = 1;
    question.QCLASS = 1;
    std::string dns_question = question.encode(question);
    int question_size = dns_question.size();
    //NOTE: may actually be htonl for the conversion. If things are not working try that
    int size = htonl(question_size);
    ssize_t sent = 0;
    ssize_t sval;
    do {
        sval = send(connectionfd, &size + sent, 4 - sent, 0);
        if (sval == -1) {
            perror("Error reading stream message");
            exit(1);
        }
        sent += sval;
    } while(sval > 0 && sent < 4);
    sent = 0;
    char message[1000];
    std::memcpy(message, dns_question.c_str(), dns_question.size());
    do {
        sval = send(connectionfd, message + sent, question_size - sent, 0);
        if (sval == -1) {
            perror("Error reading stream message");
            exit(1);
        }
        sent += sval;
    } while(sval > 0 && sent < size);
}

DNSHeader receive_dns_header(int connectionfd){
    int dns_header_size;
    ssize_t recvd = 0;
    ssize_t rval;
    do {
        rval = recv(connectionfd, &dns_header_size + recvd, 4 - recvd, 0);
        if (rval == -1) {
            perror("Error reading stream message");
            exit(1);
        }
        recvd += rval;
    } while(rval > 0 && recvd < 4);
    //NOTE: may actually be htonl for the conversion. If things are not working try that
    int header_size = ntohl(dns_header_size);
    char dns_header[1000];
    recvd = 0;
    do {
        rval = recv(connectionfd, dns_header + recvd, header_size - recvd, 0);
        if (rval == -1) {
            perror("Error reading stream message");
            exit(1);
        }
        recvd += rval;
    } while(rval > 0 && recvd < header_size);
    DNSHeader header;
    return(header.decode(dns_header));
}

DNSRecord receive_dns_record(int connectionfd){
    int dns_record_size;
    ssize_t recvd = 0;
    ssize_t rval;
    do {
        rval = recv(connectionfd, &dns_record_size + recvd, 4 - recvd, 0);
        if (rval == -1) {
            perror("Error reading stream message");
            exit(1);
        }
        recvd += rval;
    } while(rval > 0 && recvd < 4);
    //NOTE: may actually be htonl for the conversion. If things are not working try that
    int record_size = ntohl(dns_record_size);
    char dns_record[1000];
    recvd = 0;
    do {
        rval = recv(connectionfd, dns_record + recvd, record_size - recvd, 0);
        if (rval == -1) {
            perror("Error reading stream message");
            exit(1);
        }
        recvd += rval;
    } while(rval > 0 && recvd < record_size);
    DNSRecord record;
    return(record.decode(dns_record));
}

void updateIPData(const std::string& ip, const std::string& chunkName, double bitrate) {
    ipDataMap[ip] = std::make_pair(chunkName, bitrate);
}