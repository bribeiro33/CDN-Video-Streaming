#ifndef MIPROXY_HPP
#define MIPROXY_HPP

#include <unordered_map>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <utility>
#include "../starter_files/DNSHeader.h"
#include "../starter_files/DNSQuestion.h"
#include "../starter_files/DNSRecord.h"

void runProxy(int listen_port, char* server_ip, float alpha, std::fstream& log);
int setupListeningSocket(int port, struct sockaddr_in *addr);
void handleNewClient(int listen_fd, const char* server_ip, int server_port, std::vector<int> &sockets);
int connectToServer(const char* server_ip, int server_port);
int handleClientRequest(int client_fd, std::vector<int>& availableBitrates, std::fstream& log, std::vector<int> &sockets);
int handleServerResponse(int server_fd, std::vector<int>& availableBitrates, float alpha, std::fstream& log, std::vector<int> &sockets);
int loadAllData(int recv_fd, std::string &response, char* buffer, int buffer_size);
int findHeaderLength(const std::string& s);
int findContentLength(const std::string& s);
std::string modifyChunkRequestURI(  const std::string& request, double currThroughput,
                                    const std::vector<int>& availableBitrates, std::string &client_ip);
std::pair<int, int> findSegAndFragNums(const std::string& s);
void getBitrates(const std::string& response, std::vector<int>& bitrates);
std::pair<double, double> calculateThroughput(  double duration, int chunkSize, double alpha,
                                                std::string &client_ip, std::vector<int> &availableBitrates);
bool isClientSocket(int fd);
bool isServerSocket(int fd);
double getCurrentThroughput(std::string &client_ip, std::vector<int> &availableBitrates);
void getServerIP(char *dns_ip, int dns_port, char *server_ip);
// int findNewMaxFd(fd_set& fds, int listen_fd);
void send_dns_header(int connectionfd);
void send_dns_question(int connectionfd);
DNSHeader receive_dns_header(int connectionfd);
DNSRecord receive_dns_record(int connectionfd);

void updateIPData(const std::string& ip, const std::string& chunkName, double bitrate) ;

#endif // MIPROXY_HPP