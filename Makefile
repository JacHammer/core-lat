CXX = clang++
CXXFLAGS += -Wall -Wextra
CXXFLAGS += -std=c++2a -stdlib=libc++ -pthread -O0
OSTYPE?= $(shell uname -s | tr '[:upper:]' '["lower"]')
ifneq (,$(findstring linux, $(OSTYPE)))
	CXX = g++
endif
ifneq (,$(findstring darwin, $(OSTYPE)))
	CXX = clang++
endif
all:
	$(CXX) $(CXXFLAGS) -o inter-core Inter-Core.cpp

clean:
	rm -rf inter-core
