CXX = g++
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra -I$(PWD)/.. 
AR = ar
RANLIB = ranlib

ifeq ($(DEBUG), 1)
CXXFLAGS += -g -DDEBUG
endif

LIB = ../libbridge.a
LIBOBJ = ../bridge.o

all: $(LIB) server client

$(LIB): $(LIBOBJ)
	$(AR) rcs $(LIB) $(LIBOBJ)
	$(RANLIB) $(LIB) || true

$(LIBOBJ): ../bridge.cpp ../bridge.hpp
	$(CXX) $(CXXFLAGS) -c -o $(LIBOBJ) ../bridge.cpp

server: server.cpp $(LIB)
	$(CXX) $(CXXFLAGS) -o server server.cpp $(LIB)

client: client.cpp $(LIB)
	$(CXX) $(CXXFLAGS) -o client client.cpp $(LIB)

clean:
	rm -f server client $(LIBOBJ) $(LIB)
