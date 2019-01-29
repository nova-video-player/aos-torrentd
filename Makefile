CXX=clang
CXXFLAGS+=-std=c++11 -fPIC -g -Wall
LDLIBS=-lstdc++ -lpthread -ltorrent-rasterbar -lboost_system
LDFLAGS=-fPIC

all: torrentd

torrentd: torrentd.o httpd.o
