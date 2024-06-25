CXX = g++
CXXFLAGS = -std=c++14 -g -pedantic 

all: miProxy

miProxy: miProxy.cpp
	$(CXX) $(CXXFLAGS) miProxy.cpp -o miProxy

clean:
	rm -rf miProxy *.dSYM

.PHONY: clean