#include <string.h>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>
#include <fstream>
#include <limits>
#include <iostream>
#include "../starter_files/DNSHeader.h"
#include "../starter_files/DNSQuestion.h"
#include "../starter_files/DNSRecord.h"

std::vector<std::string> round_robin_servers;
int num_rr_requests = 0;

int num_nodes;

struct Node{
    int host_id;
    std::string node_type;
    std::string ip_address;
};

std::vector<Node> nodes;
std::vector<int> destination_vertices;

struct Link{
    int origin_id;
    int destination_id;
    int cost;
};

std::vector<std::vector<int> > topology;
std::vector<bool> visited;

// recvs() integer designating size of DNS Header
int receive_size_dns_header(int connectionfd){
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
    int header_size = ntohl(dns_header_size);
    return header_size;
}

// recvs() DNS header via decode()
DNSHeader receive_dns_header(int connectionfd, int header_size){
    char dns_header[1000];
    ssize_t recvd = 0;
    ssize_t rval;
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

// recvs() integer designating size of DNS Question
int receive_size_dns_question(int connectionfd){
    int dns_question_size;
    ssize_t recvd = 0;
    ssize_t rval;
    do {
        rval = recv(connectionfd, &dns_question_size + recvd, 4 - recvd, 0);
        if (rval == -1) {
            perror("Error reading stream message");
            exit(1);
        }
        recvd += rval;
    } while(rval > 0 && recvd < 4);
    int question_size = ntohl(dns_question_size);
    return question_size;
}

// recvs() DNS Question via decode()
DNSQuestion receive_dns_question(int connectionfd, int question_size){
    char dns_question[1000];
    ssize_t recvd = 0;
    ssize_t rval;
    do {
        rval = recv(connectionfd, dns_question + recvd, question_size - recvd, 0);
        if (rval == -1) {
            perror("Error reading stream message");
            exit(1);
        }
        recvd += rval;
    } while(rval > 0 && recvd < question_size);
    DNSQuestion question;
    return(question.decode(dns_question));
}

// selects server on round robin selection
std::string round_robin_server_selection(){
    size_t num_servers = round_robin_servers.size();
    std::string selected_server = round_robin_servers[num_rr_requests % num_servers];
    num_rr_requests++;
    return selected_server;
}

int minDistance(std::vector<int> dist){
    int min = std::numeric_limits<int>::max();
    int min_index;
    for (int v = 0; v < num_nodes; ++v){
        if (visited[v] == false && dist[v] <= min){
            min = dist[v];
            min_index = v;
        }
    }
    return min_index;
}

//finds the shortest distance to each vertex from the source vertex
//NOTE: uses global variables int num_nodes, vector<bool>visited, and vector<vector<int> > topology, and vector<int> destination_vertices
int dijkstra(int source){
    //distance array with shortest distances to each vertex from source vertex
    std::vector<int> dist(num_nodes, 0);

    for (int i = 0; i < num_nodes; ++i){
        dist[i] = std::numeric_limits<int>::max();
        visited[i] = false;
    }

    dist[source] = 0;

    for (int count = 0; count < num_nodes - 1; count++) {
        int u = minDistance(dist);

        visited[u] = true;
        for (int v = 0; v < num_nodes; v++){
            if (visited[v] == false && topology[u][v] > 0 && dist[u] != std::numeric_limits<int>::max() && dist[u] + topology[u][v] < dist[v]){
                dist[v] = dist[u] + topology[u][v];
            }
        }
    }

    int min = std::numeric_limits<int>::max();
    int min_index = -1;
    //find the server with the shortest distance from source
    for (int i = 0; i < destination_vertices.size(); ++i){
        int index = destination_vertices[i];
        if (dist[index] < min){
            min = dist[index];
            min_index = index;
        }
    }
    if (min_index == -1){
        perror("no server indeces");
        exit(1);
    }
    return min_index;
}

// selects server on geographical distance selection
std::string geographic_distance_server_selection(std::string client_ip){
    int source_id = -1;
    for (size_t i = 0; i < nodes.size(); ++i){
        if (nodes[i].ip_address == client_ip){
            source_id = nodes[i].host_id;
            break;
        }
    }
    if (source_id == -1){
        perror("invalid client_ip");
        exit(1);
    }
    int destination_id = dijkstra(source_id);
    return nodes[destination_id].ip_address;
}

// encode dns header to send
std::string dns_header_rcode3(DNSHeader header){
    header.AA = 1;
    header.QR = 1;
    header.RD = 0;
    header.RA = 0;
    header.Z = 0;
    header.RCODE = 3;
    header.NSCOUNT = 0;
    header.ARCOUNT = 0;
    std::string dns_header = header.encode(header);
    return dns_header;
}

std::string update_dns_header(DNSHeader header){
    header.QR = 1;
    header.AA = 1;
    header.RD = 0;
    header.RA = 0;
    header.Z = 0;
    header.RCODE = 0;
    header.NSCOUNT = 0;
    header.ARCOUNT = 0;
    std::string dns_header = header.encode(header);
    return dns_header;
}

// sends integer designating size of DNS Header
void send_size_dns_header(int connectionfd, int dns_header_size){
    ssize_t sent = 0;
    ssize_t sval;
    int size = htonl(dns_header_size);
    do {
        sval = send(connectionfd, &size + sent, 4 - sent, 0);
        if (sval == -1) {
            perror("Error reading stream message");
            exit(1);
        }
        sent += sval;
    } while(sval > 0 && sent < 4);
}

// sends encoded DNS Header
void send_dns_header(int connectionfd, int size, std::string dns_header){
    char header[1000];
    std::memcpy(header, dns_header.c_str(), dns_header.size());
    ssize_t sent = 0;
    ssize_t sval;
    do {
        sval = send(connectionfd, header + sent, size - sent, 0);
        if (sval == -1) {
            perror("Error reading stream message");
            exit(1);
        }
        sent += sval;
    } while(sval > 0 && sent < size);
}

// sends integer designating size of DNS Record
void send_size_dns_record(int connectionfd, int dns_record_size){
    ssize_t sent = 0;
    ssize_t sval;
    int size = htonl(dns_record_size);
    do {
        sval = send(connectionfd, &size + sent, 4 - sent, 0);
        if (sval == -1) {
            perror("Error reading stream message");
            exit(1);
        }
        sent += sval;
    } while(sval > 0 && sent < 4);
}

// sends DNS Record via encode()
void send_dns_record(int connectionfd, int size, std::string dns_record){
    char record[1000];
    std::memcpy(record, dns_record.c_str(), dns_record.size());
    ssize_t sent = 0;
    ssize_t sval;
    do {
        sval = send(connectionfd, record + sent, size - sent, 0);
        if (sval == -1) {
            perror("Error reading stream message");
            exit(1);
        }
        sent += sval;
    } while(sval > 0 && sent < size);
}

int make_server_sockaddr(struct sockaddr_in *addr, int port) {
    // Step (1): specify socket family.
    // This is an internet socket.
    addr->sin_family = AF_INET;

    // Step (2): specify socket address (hostname).
    // The socket will be a server, so it will only be listening.
    // Let the OS map it to the correct address.
    addr->sin_addr.s_addr = INADDR_ANY;

    // Step (3): Set the port value.
    // If port is 0, the OS will choose the port for us.
    // Use htons to convert from local byte order to network byte order.
    addr->sin_port = htons(port);

    return 0;
}

int handle_connection(int connectionfd, std::fstream &log, char *flag, std::string client_ip){
    // recvs() integer designating size of DNS Header
    int dns_header_size = receive_size_dns_header(connectionfd);
    DNSHeader dns_header = receive_dns_header(connectionfd, dns_header_size);
    int dns_question_size = receive_size_dns_question(connectionfd);
    DNSQuestion dns_question = receive_dns_question(connectionfd, dns_question_size);
    std::string query_name = dns_question.QNAME;
    std::string response_ip;
    if (query_name != "video.cse.umich.edu"){
        std::string dns_header_to_send = dns_header_rcode3(dns_header);
        dns_header_size = dns_header_to_send.size();
        send_size_dns_header(connectionfd, dns_header_size);
        send_dns_header(connectionfd, dns_header_size, dns_header_to_send);
        close(connectionfd);
        return 0;
    }
    if (strcmp(flag, "rr") == 0){
        response_ip = round_robin_server_selection();
    }
    else if (strcmp(flag, "geo") == 0){
        response_ip = geographic_distance_server_selection(client_ip);
    }
    else{
        perror("invalid flag");
        exit(1);
    }
    std::string dns_header_to_send = update_dns_header(dns_header);
    dns_header_size = dns_header_to_send.size();
    send_size_dns_header(connectionfd, dns_header_size);
    send_dns_header(connectionfd, dns_header_size, dns_header_to_send);
    DNSRecord record;
    std::memcpy(record.NAME, dns_question.QNAME, query_name.size());
    record.TYPE = 1;
    record.CLASS = 1;
    record.TTL = 0;
    record.RDLENGTH = response_ip.size();
    std::memcpy(record.RDATA, response_ip.c_str(), response_ip.size());
    std::string dns_record = record.encode(record);
    int dns_record_size = dns_record.size();
    log << client_ip << " " << query_name << " " << response_ip << std::endl;
    send_size_dns_record(connectionfd, dns_record_size);
    send_dns_record(connectionfd, dns_record_size, dns_record);
    close(connectionfd);
    return 0;
}

int run_dns(int port, std::fstream &log, char *flag){
    // (1) Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1) {
        perror("Error opening stream socket");
        return -1;
    }
    // (2) Set the "reuse port" socket option
    int yesval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yesval, sizeof(yesval)) == -1) {
        perror("Error setting socket options");
        return -1;
    }
    // (3) Create a sockaddr_in struct for the proper port and bind() to it.
    struct sockaddr_in addr;
    socklen_t len;
    if (make_server_sockaddr(&addr, port) == -1) {
        return -1;
    }
    // (3b) Bind to the port.
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("Error binding stream socket");
        return -1;
    }
    // (4) Begin listening for incoming connections.
    if (listen(sockfd, 10) == -1) {
        perror("Error listening");
        return -1;
    }
    // (5) Serve incoming connections one by one forever.
    len = sizeof(addr);
    int connectionfd = accept(sockfd, (struct sockaddr*)&addr, (socklen_t*)&len);
    if (connectionfd == -1) {
        perror("Error accepting connection");
        return -1;
    }
    // get the peer name for client ip address.
    getpeername(sockfd, (struct sockaddr*)&addr, &len);
    std::string client_ip = inet_ntoa(addr.sin_addr);
    if (handle_connection(connectionfd, log, flag, client_ip) == -1) {
        return -1;
    }
    close(sockfd);
    return 0;
}

int main(int argc, char **argv){
    if (argc != 5){
        perror("Error: missing or extra arguments\n");
        exit(1);
    }
    std::fstream servers;
    std::fstream log;
    int port = atoi(argv[2]);
    servers.open(argv[3], std::fstream::in);
    if (!servers.is_open()){
        perror("failed to open servers");
        exit(1);
    }
    log.open(argv[4], std::fstream::out);
    if (!log.is_open()){
        perror("failed to open log");
        exit(1);
    }
    char *flag;
    if (strcmp(argv[1], "--geo") == 0){
        flag = (char*)"geo";
        std::string str;
        servers >> str;
        //NOTE: num_nodes is a global variable
        servers >> num_nodes;
        for (int i = 0; i < num_nodes; ++i){
            Node node;
            servers >> node.host_id;
            servers >> node.node_type;
            servers >> node.ip_address;
            nodes.push_back(node);
            //get a list of nodes that can be final destinations
            if (node.node_type == "SERVER"){
                destination_vertices.push_back(node.host_id);
            }
        }
        topology.resize(num_nodes);
        for (int i = 0; i < num_nodes; ++i){
            topology[i].resize(num_nodes, 0);
        }
        visited.resize(num_nodes, false);
        servers >> str;
        int num_links;
        servers >> num_links;
        for (int i = 0; i < num_links; ++i){
            Link link;
            servers >> link.origin_id;
            servers >> link.destination_id;
            servers >> link.cost;
            if (nodes[link.origin_id].node_type == "CLIENT"){
                //client->switch or client->server
                if (nodes[link.destination_id].node_type == "SWITCH" || nodes[link.destination_id].node_type == "SERVER"){
                    topology[link.origin_id][link.destination_id] = link.cost;
                }
            }
            else if (nodes[link.origin_id].node_type == "SWITCH"){
                //switch<-client
                if (nodes[link.destination_id].node_type == "CLIENT"){
                    topology[link.destination_id][link.origin_id] = link.cost;
                }
                //switch->server
                else if (nodes[link.destination_id].node_type == "SERVER"){
                    topology[link.origin_id][link.destination_id] = link.cost;
                }
                //switch<->switch
                else if (nodes[link.destination_id].node_type == "SWITCH"){
                    topology[link.origin_id][link.destination_id] = link.cost;
                    topology[link.destination_id][link.origin_id] = link.cost;
                }
            }
            else if (nodes[link.origin_id].node_type == "SERVER"){
                //server<-client or server<-switch
                if (nodes[link.destination_id].node_type == "CLIENT" || nodes[link.destination_id].node_type == "SWITCH"){
                    topology[link.destination_id][link.origin_id] = link.cost;
                }
            }
            
        }
    }
    else if (strcmp(argv[1], "--rr") == 0){
        flag = (char*)"rr";
        std::string rr_server_ip;
        while(std::getline(servers, rr_server_ip)){
            round_robin_servers.push_back(rr_server_ip);
        }
    }
    else{
        perror("Error: invalid flag\n");
        exit(1);
    }

    servers.close();
    while(true){
        run_dns(port, log, flag);
    }
}