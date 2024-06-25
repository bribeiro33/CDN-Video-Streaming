CXX = g++
CXXFLAGS = -std=c++14 -g -pedantic 

all: nameserver

nameserver: nameserver.cpp
	$(CXX) $(CXXFLAGS) nameserver.cpp -o nameserver

clean:
	rm -rf nameserver *.dSYM

.PHONY: clean