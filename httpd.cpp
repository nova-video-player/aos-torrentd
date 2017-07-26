// Copyright 2017 Archos SA
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#define BOOST_ASIO_SEPARATE_COMPILATION
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <time.h>
#include <string.h>

#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <condition_variable>
#include <unordered_map>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

class SocketHelper {
	private:
		int _fd;
		uint8_t buffer[1024];
		int _pos, _size;
	public:
		SocketHelper(int fd) : _fd(fd), _pos(0), _size(0) { };
		void pop() {
			if(_size == 0)
				return;
			_pos++;
			_size--;
			_pos%=sizeof(buffer);
		}

		int getc() {
			int v = next();
			pop();
			return v;
		}

		int next() {
			if(_size==0)
				readMore();
			if(_size==0)
				return -1;
			return buffer[_pos];
		}

		void readMore() {
			if(_size)
				return;
			int n = sizeof(buffer)-_pos;
			int got = read(_fd, &buffer[_pos], n);
			_size += got;
		}

		std::string getLine() {
			std::string res="";
			while(1) {
				int v = getc();
				if(v == -1)
					return res;

				if(v == '\r' && next() == '\n') {
					pop();
					return res;
				}
				res += (char)v;
			}
		}

		void writeString(const char *str) {
			write(_fd, str, strlen(str));
		}
};

static std::unordered_map<std::string, std::string> getCommand(SocketHelper& stream) {
	std::unordered_map<std::string, std::string> res;
	std::string line = stream.getLine();

	std::vector<std::string> strs;
	boost::split(strs, line, boost::is_any_of("\t "));
	if(strs.size() < 3) {
		res["error"] = "Invalid protocol";
		return res;
	}

	res["method"] = strs[0];
	res["file"] = strs[1];

	return res;
}

static std::unordered_map<std::string, std::string> getRequest(SocketHelper& stream) {
	auto res = getCommand(stream);
	if(res.count("error"))
		return res;
	while(true) {
		std::string line = stream.getLine();
		if(line=="")
			return res;

		const char *str = line.c_str();
		char *key = strdup(str);
		char *sep = strchr(key, ':');
		if(!sep) {
			std::cerr << "Failed to parse " << line << std::endl;
			res["error"] = "Failed to parse header";
			return res;
		}
		*sep = 0;
		sep++;
		if(*sep)
			sep++;
		res[key] = sep;
		free(key);
	}
	return res;
}

std::pair<long long,long long> parseRange(const std::string& str) {
	auto res = std::make_pair(0LL, -1LL);
	auto cstr = str.c_str();

	if(strstr(cstr, "bytes=") != cstr)
		return res;

	cstr += strlen("bytes=");
	std::cerr << "cstr = " << cstr << std::endl;

	char *next = NULL;
	res.first = strtoll(cstr, &next, 0);
	if(!next || *next != '-')
		return res;

	next++;
	char *next2 = NULL;
	res.second = strtoll(next, &next2, 0);
	if(next == next2)
		res.second = -1;

	return res;
}

static void giveContentLength(SocketHelper& fd, std::pair<long long, long long> range);
static void serveFile(std::unordered_map<std::string, std::string>& request, int fd, std::pair<long long,long long> range);
static void clientHandler(int fd) {
	SocketHelper stream(fd);
	auto request = getRequest(stream);
	if(request.count("error")) {
		stream.writeString("Protocol fail...\n\r");
		close(fd);
		return;
	}
	std::cerr << "Request =" << std::endl;
	for(auto it = request.begin(); it != request.end(); ++it) {
		std::cerr << "\t" << it->first << " = " << it->second << std::endl;
	}
	auto range = parseRange(request["Range"]);
	std::cerr << "Parsed range = " << range.first << ":" << range.second << std::endl;
	if(range.first)
		stream.writeString("HTTP/1.0 206 Partial Content\r\n");
	else
		stream.writeString("HTTP/1.0 200 OK\r\n");
	stream.writeString("Server: Bittorrent2Http\r\n");
	stream.writeString("Connection: close\r\n");
	stream.writeString("Accept-Ranges: bytes\r\n");
	giveContentLength(stream, range);
	stream.writeString("\r\n");

	serveFile(request, fd, range);
	close(fd);
}

static std::mutex fileInfos_l;
static std::condition_variable fileInfos_v;
static const char *_filePath = NULL;
//static int _maxOffset = 0;
static long long _fileSize = 0;
static std::function<long long (long long, long long)> _availableData;

static std::mutex _currentRanges_l;
static std::list<std::pair<long long, long long> > _currentRanges;

static void dumpCurrentRanges() {
	std::unique_lock<std::mutex> lk(_currentRanges_l);
	std::cerr << "Ranges:" << std::endl;
	for(auto it = _currentRanges.begin(); it != _currentRanges.end(); ++it) {
		std::cerr << "\t" << it->first << "-" << it->second << std::endl;
	}
}

static void giveContentLength(SocketHelper& fd, std::pair<long long, long long> range) {
	std::unique_lock<std::mutex> lk(fileInfos_l, std::defer_lock);

	long long fileSize = 0;
	lk.lock();
	while(1) {
		const char* filePath = _filePath;
		//int maxOffset = _maxOffset;
		fileSize = _fileSize;
		if(filePath)
			break;
		fileInfos_v.wait(lk);
	}
	lk.unlock();

	std::string str = "Content-Length: ";
	long long size = 0;
	if(range.second == -1)
		size = fileSize - range.first;
	else
		size = range.second;
	str += boost::lexical_cast<std::string>(size);
	str += "\r\n";
	std::cerr << "Add " << str << std::endl; 
	fd.writeString(str.c_str());

	if(range.first) {
		str = "Content-Range: ";
		//Start
		str += boost::lexical_cast<std::string>(range.first);
		str += "-";
		//-End
		if(range.second)
			str += boost::lexical_cast<std::string>(range.second);
		else
			str += boost::lexical_cast<std::string>(fileSize);
		// /size
		str += "/";
		str += boost::lexical_cast<std::string>(size);
		str += "\r\n";
		fd.writeString(str.c_str());
	}
}

static void insertRange(std::pair<long long, long long> range) {
	std::unique_lock<std::mutex> lk(_currentRanges_l);
	_currentRanges.push_back(range);
}

static void deleteRange(std::pair<long long, long long> range) {
	std::unique_lock<std::mutex> lk(_currentRanges_l);
	for(auto it = _currentRanges.begin(); it != _currentRanges.end(); ++it) {
		if(*it == range) {
			_currentRanges.erase(it);
			return;
		}
	}
	std::cerr << "Couldn't find range to be deleted" << std::endl;
}

std::list<std::pair<long long, long long> > getRanges() {
	std::unique_lock<std::mutex> lk(_currentRanges_l);
	auto res = _currentRanges;
	return res;
}

static void serveFile(std::unordered_map<std::string, std::string>& request, int fd, std::pair<long long,long long> range) {
	long long currentOffset = range.first;
	int file = -1;
	std::unique_lock<std::mutex> lk(fileInfos_l, std::defer_lock);

	insertRange(range);

	dumpCurrentRanges();

	lk.lock();
	while(1) {
		const char* filePath = _filePath;
		//int maxOffset = _maxOffset;
		auto fileSize = _fileSize;
		auto availableData = _availableData;
		lk.unlock();
		std::cerr << "File path " << filePath << std::endl;

		if(file == -1 && filePath) {
			file = open(filePath, O_RDONLY);
			perror("Opening file");
			lseek(file, range.first, SEEK_SET);
			perror("Seeking file");
		}

		int fail = 0;
		//while(filePath && file != -1 && currentOffset < maxOffset && (currentOffset < range.second || range.second == -1LL)) {
		while(filePath && file != -1 && (currentOffset < range.second || range.second == -1LL)) {
			char buffer[1024];
			int length = availableData(currentOffset, sizeof(buffer));
			if(!length)
				break;
			if(length > sizeof(buffer))
				length = sizeof(buffer);
			int res = read(file, buffer, length);
			if(!res || res == -1) {
				perror("read");
				fail = 1;
				break;
			}
			int res2 = write(fd, buffer, res);
			if(!res2 || res2 == -1) {
				perror("write");
				fail = 1;
				break;
			}

			currentOffset += res;
		}
		deleteRange(range);
		range.first = currentOffset;
		insertRange(range);
		dumpCurrentRanges();

		if(fail)
			break;

		if(filePath && currentOffset == fileSize)
			break;


		std::cerr << "currentOffset = " << currentOffset
			<< ", fileSize = " << fileSize
			<< std::endl;

		if(currentOffset >= range.second &&
				range.second != -1LL)
			break;

		lk.lock();
		//TODO: his might lead to a case where the connection is closed
		//But we're still waiting here
		fileInfos_v.wait(lk);

#if 1
		//Check connection not dead
		char c;
		if(recv(fd, &c, 1, MSG_PEEK|MSG_DONTWAIT) < 0 && errno != EAGAIN) {
			perror("recv");
			break;
		}
#endif
	}

	deleteRange(range);

	dumpCurrentRanges();
	close(file);
}

void setFileInfos(const char *filePath, long long fileSize, std::function<long long(long long, long long)> availableData) {
	fileInfos_l.lock();

	if(_filePath)
		delete _filePath;
	_filePath = strdup(filePath);
	//_maxOffset = maxOffset;
	_fileSize = fileSize;
	_availableData = availableData;

	fileInfos_l.unlock();
	fileInfos_v.notify_all();
}

static void httpd() {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1) {
		std::cerr << "Could not create socket" << std::endl;
		return;
	} else {
		std::cerr << "Server started" << std::endl;
	}

	// Prepare the sockaddr_in structure
	struct sockaddr_in s_addr;
	s_addr.sin_family = AF_INET;
	s_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	//s_addr.sin_port = htons(19992);
	int port = 10000 + (time(NULL) % 10000);
	std::cout << port << std::endl;
	s_addr.sin_port = htons(port);
	bind(fd, (struct sockaddr*) &s_addr, sizeof(s_addr));
	perror("bind");
	listen(fd, 10);

	while(1) {
		struct sockaddr_in c_addr;
		socklen_t len = sizeof(c_addr);
		int cfd = accept(fd, (struct sockaddr*)&c_addr, &len);
		std::thread clientThread(clientHandler, cfd);
		clientThread.detach();
	}
}

void start_httpd() {
	std::thread t(httpd);
	t.detach();
}
