# Video Streaming via CDN
## Overview
This project implements a simplified version of a Content Distribution Network (CDN) for video streaming. The project is divided into two main parts:

1. Bitrate Adaptation in HTTP Proxy
2. DNS Load Balancing

Uses a virtual machine in  VMware Workstation Player that includes Mininet, Apache, and all necessary files.
## Bitrate Adaptation in HTTP Proxy
Implements an HTTP proxy (miProxy) to intercept video chunk requests and adaptively select the appropriate bitrate based on real-time throughput calculations. Measures throughput using an exponentially-weighted moving average (EWMA).
### Running miProxy
1. Without DNS functionality:
```./miProxy --nodns <listen-port> <www-ip> <alpha> <log>```

2. With DNS functionality:
```./miProxy --dns <listen-port> <dns-ip> <dns-port> <alpha> <log>```

Logging Format
```<browser-ip> <chunkname> <server-ip> <duration> <tput> <avg-tput> <bitrate>```

## DNS Load Balancing
Implements a DNS server (nameserver) with round-robin and geographic distance-based load balancing strategies.
### Running nameserver
1. Round-Robin Load Balancer:
```./nameserver --rr <port> <servers> <log>```

2. Geographic Distance Load Balancer:
```./nameserver --geo <port> <servers> <log>```

Logging Format
```<client-ip> <query-name> <response-ip>```

### Testing
Starting the Apache Server
```python start_server.py <host_number>```

Launching Firefox
```python launch_firefox.py <profile_num>```

Testing the DNS Server
```<path to the binary>/queryDNS <IP of nameserver> <port of nameserver>```
