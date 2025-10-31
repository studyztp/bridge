CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I.. 
AR = ar
RANLIB = ranlib
LDFLAGS = -pthread

LIB = ../libbridge.a
LIBOBJ = ../bridge.o

all: $(LIB) test_framing test_socket_io

test_integration: test_integration.cpp $(LIB)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o test_integration test_integration.cpp $(LIB)

$(LIB): $(LIBOBJ)
	$(AR) rcs $(LIB) $(LIBOBJ)
	$(RANLIB) $(LIB) || true

$(LIBOBJ): ../bridge.cpp ../bridge.hpp
	$(CXX) $(CXXFLAGS) -c -o $(LIBOBJ) ../bridge.cpp

test_framing: test_framing.cpp $(LIB)
	$(CXX) $(CXXFLAGS) -o test_framing test_framing.cpp $(LIB)

test_socket_io: test_socket_io.cpp $(LIB)
	$(CXX) $(CXXFLAGS) -o test_socket_io test_socket_io.cpp $(LIB)

clean:
	rm -f test_framing test_socket_io $(LIBOBJ) $(LIB)
