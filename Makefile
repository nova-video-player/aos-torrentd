CXX=clang
CXXFLAGS=-I/home/phh/Torrent/libtorrent-rasterbar-1.0.3/include
CXXFLAGS+=-std=c++11 -fPIC -g
LDLIBS=-lstdc++ -lpthread /home/phh/Torrent/libtorrent-rasterbar-1.0.3/bin/gcc-4.9.1/debug/link-static/threading-multi/libtorrent.a -lboost_system
LDFLAGS=-fPIC

all: torrentd

torrentd: torrentd.o httpd.o
