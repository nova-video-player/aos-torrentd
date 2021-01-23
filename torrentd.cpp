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

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <linux/prctl.h>
#include <sys/prctl.h>
#include <iostream>
#include "libtorrent/alert.hpp"
#include "libtorrent/alert_types.hpp"
#include "libtorrent/announce_entry.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/magnet_uri.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/time.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/ip_filter.hpp"
//#include "libtorrent/extensions/lt_trackers.hpp"
#include "libtorrent/extensions/smart_ban.hpp"
#include "libtorrent/extensions/ut_metadata.hpp"
#include "libtorrent/extensions/ut_pex.hpp"

#ifdef __ANDROID__
extern "C" {
ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
	char *ptr;
	ptr = fgetln(stream, n);
	if (ptr == NULL) {
		return -1;
	}
	/* Free the original ptr */
	if (*lineptr != NULL) free(*lineptr);
	/* Add one more space for '\0' */
	size_t len = n[0] + 1;
	/* Update the length */
	n[0] = len;
	/* Allocate a new buffer */
	*lineptr = (char*) malloc(len);
	/* Copy over the string */
	memcpy(*lineptr, ptr, len-1);
	/* Write the NULL character */
	(*lineptr)[len-1] = '\0';
	/* Return the length of the new buffer */
	return len;
}
};
#endif


using namespace libtorrent;
extern void start_httpd();
extern void setFileInfos(const char *filePath, long long fileSize, std::function<long long (long long, long long)>);
extern std::list<std::pair<long long, long long> > getRanges();

static session* _myLibtorrentSession;
session* s() {
	if(!_myLibtorrentSession)
		_myLibtorrentSession = new session();
	return _myLibtorrentSession;
}

void end(int sig) {
	(void)sig;
    auto state = s()->session_state();

    auto session_state = write_session_params(state);

	std::vector<char> out;
	bencode(std::back_inserter(out), session_state);
	int fd = open(".ses_state", O_WRONLY|O_CREAT, 0644);
	write(fd, &out[0], out.size());
	close(fd);
	std::cerr << "Saved state" << std::endl;
	_exit(1);
}

static void setup() {
	auto pack = s()->get_settings();

	pack.set_int(settings_pack::connections_limit, 50);
	pack.set_int(settings_pack::upload_rate_limit, 200*1024);
	pack.set_int(settings_pack::request_timeout, 20);
	pack.set_str(settings_pack::dht_bootstrap_nodes,
			"router.bittorrent.com:6881,"
			"router.utorrent.com:6881,"
			"router.bitcomet.com:6881,"
			"dht.transmissionbt.com:6881");

	pack.set_int(settings_pack::max_retry_port_bind, 20);
	pack.set_str(settings_pack::listen_interfaces, "[::]:9042");
	pack.set_bool(settings_pack::enable_natpmp, true);
	pack.set_bool(settings_pack::enable_upnp, true);
	pack.set_bool(settings_pack::enable_lsd, true);
	pack.set_bool(settings_pack::enable_dht, true);
	pack.set_int(settings_pack::alert_mask, alert::error_notification);

	s()->apply_settings(pack);
}

static int init_torrentd() {
	error_code ec;
	{
		int loadFd = open(".ses_state", O_RDONLY);
		if(loadFd != -1) {
			std::vector<char> in;
			off_t end = lseek(loadFd, 0, SEEK_END);
			lseek(loadFd, 0, SEEK_SET);
			in.resize(end);
			read(loadFd, &in[0], end);

			bdecode_node e = bdecode(in, ec);
			if(ec) {
				fprintf(stderr, "failed loading saved state: %s\n", ec.message().c_str());
			} else {
				std::cerr << "Loading saved state..." << std::endl;
                auto params = read_session_params(in);
                if(_myLibtorrentSession) std::cerr << "Can't restore session because it is already started" << std::endl;
                _myLibtorrentSession = new session(params);
			}
		}
	}

	s()->add_extension(&libtorrent::create_ut_metadata_plugin);
	s()->add_extension(&libtorrent::create_ut_pex_plugin);
	s()->add_extension(&libtorrent::create_smart_ban_plugin);

	setup();

	return 0;
}

static void load_blocklist(char *path) {
	FILE* filter = fopen("blocklist", "r");
	if(!filter)
		return;

	ip_filter fil;
	char *line = NULL;
	size_t len = 0;

	while(getline(&line, &len, filter)>=0) {
		char *str = strchr(line, ':');
		if(!str)
			continue;
		str++;
		unsigned int a,b,c,d,e,f,g,h;
		if(sscanf(str, "%u.%u.%u.%u-%u.%u.%u.%u", &a, &b, &c, &d, &e, &f, &g, &h) != 8)
			continue;

		address_v4 start((a << 24) + (b << 16) + (c << 8) + d);
		address_v4 last((e << 24) + (f << 16) + (g << 8) + h);
		fil.add_rule(start, last, ip_filter::blocked);
	}

	s()->set_ip_filter(fil);
	fclose(filter);
}

static void add_torrent(const char* torrent) {
	error_code ec;
	add_torrent_params p;
	p.save_path = "./";

    //This one is if "torrent" is a local file
	p.ti.reset(new torrent_info(torrent, ec));

    //if it failed then try other things
	if(ec) {
		p.ti = nullptr;
        //Ask to parse it as http:// <DEPRECATED>
		p.url = torrent;

		//Try to parse it as a magnet
		parse_magnet_uri(torrent, p, ec);
	}

	s()->async_add_torrent(p);
}

int main(int argc, char* argv[])
{
	if(argc<=2) {
		std::cerr << argv[0] << ": <torrent url or magnet> <pathtoblocklist>" << std::endl;
		exit(1);
	}

	if(init_torrentd())
		return 1;

	//If parent dies, we get a SIGHUP
	prctl(PR_SET_PDEATHSIG, SIGHUP);
	signal(SIGINT, end);
	signal(SIGHUP, end);
	signal(SIGPIPE, SIG_IGN);
	start_httpd();

	load_blocklist(argv[2]);

	add_torrent(argv[1]);

	int fileId = -1;

	struct {
		int pieceLength;
		int nTotalPieces;
		long long offset;
		int nPieces;
		int firstPiece;
		int lastPiece;
		long long fileSize;
		const char *path;
		int nTrackers;
	} infos;
	
	//Event loop
	while(1) {
		if(!s()->wait_for_alert(milliseconds(1000))) {
			s()->post_torrent_updates();
			continue;
		}

		std::vector<alert*> alerts;
		s()->pop_alerts(&alerts);
		for(auto alert: alerts) {
			if (state_update_alert* p = alert_cast<state_update_alert>(alert)) {
				for (std::vector<torrent_status>::iterator i = p->status.begin();
						i != p->status.end(); ++i) {
					auto torrentInfo = i->torrent_file.lock();
					if(!torrentInfo->is_valid()) {
						std::cerr << "Torrent not valid yet" << std::endl;
						continue;
					}
					auto& hdl = i->handle;

					if(!(hdl.flags() & torrent_flags::sequential_download)) {
						hdl.set_flags(torrent_flags::sequential_download, torrent_flags::sequential_download);
					}

					// One-time tasks:
					// - Find fileId
					// - Retrieve static torrent infos
					if(fileId == -1) {
						auto files = torrentInfo->files();
						for(int j=0; j<files.num_files(); ++j) {
							std::cout << files.file_path(j) << std::endl;
						}
						//Empty line to mark end of list
						std::cout << std::endl;
						std::cerr << "More than one file, which one to take ?" << std::endl;
						std::cin >> fileId;

						infos.pieceLength = torrentInfo->piece_length();
						infos.nTotalPieces = torrentInfo->num_pieces();

						infos.offset = files.file_offset(fileId);
						infos.nPieces = (files.file_size(fileId) + infos.pieceLength - 1)/infos.pieceLength;
						infos.firstPiece = infos.offset/infos.pieceLength;
						infos.fileSize = files.file_size(fileId);
						infos.lastPiece = (infos.offset + infos.fileSize)/infos.pieceLength;
						infos.path = strdup(files.file_path(fileId).c_str());

						auto trackers = torrentInfo->trackers();
						infos.nTrackers = trackers.size();
					}

					//Compute pieces priorities
					std::vector<download_priority_t> priorities(infos.nTotalPieces, dont_download);
					//Please note that streaming mode is on
					//So early pieces are prefered by default
					auto ranges = getRanges();

					//Set all pieces in the file to default priority
					for(int j = infos.firstPiece;
							j<= infos.lastPiece && j <infos.nTotalPieces;
							++j)
						priorities[j] = low_priority;
					//We will most likely need the end of the file
					//Either because of mkv/mp4, or to fingerprint subtitles
					if(infos.lastPiece >= infos.nTotalPieces) {
						std::cerr << "lastPiece >= TotalPieces" << std::endl;
					} else {
						priorities[infos.lastPiece] = top_priority;
					}

					//To support seeking, we do two things:
					//- Highly prioritize 10MB around current data cursor
					// // - We determine lowest requested byte, so we can null-prioritize data already skipped
					long long earliest = infos.fileSize;
					for(auto it = ranges.begin(); it != ranges.end(); ++it) {
						if(it->first < earliest)
							earliest = it->first;
						int TenMB_in_pieces = (10*1024*1024)/infos.pieceLength;
						if(!TenMB_in_pieces)
							TenMB_in_pieces = 1;
						int pieceN = (it->first + infos.offset)/infos.pieceLength;

						//Ask for 10MB max priority
						//In streaming mode, only priority 7 is taken in account
						for(int j = 0; j < TenMB_in_pieces; ++j) {
							int pos = j+pieceN;
							if( pos > infos.lastPiece)
								break;
							priorities[pos] = top_priority;
						}
					}

					//If no socket is open yet, assume no seeking
					if(ranges.empty())
						earliest = 0;

					//Now that we have computed priorities, tell libtorrent about it
					i->handle.prioritize_pieces(priorities);

					int nPeers = i->list_peers;
					if(nPeers == 0) {
						if(infos.nTrackers == 0) {
							nPeers = -1;
						} else if(i->current_tracker == "") {
							nPeers = -2;
						}
					}

					std::cout
						<< i->num_peers << ";"
						<< nPeers << ";"
						<< i->download_rate << ";"
						<< (hdl.flags() & torrent_flags::seed_mode) << ";"
						<< i->total_wanted_done << ";"
						<< i->total_wanted << ";"
						<< i->distributed_full_copies << std::endl;


					std::cerr << i->name
						<< ":" << (hdl.flags() & torrent_flags::sequential_download)
						<< ":" << (i->total_payload_download/1024)
						<< "/" << (infos.fileSize/1024)
						<< "\n\tfileSize = " << infos.fileSize
						<< "\n\tnTotalPieces = " << infos.nTotalPieces
						<< "\n\tfirstPiece = " << infos.firstPiece
						<< "\n\tlastPiece = " << infos.lastPiece
						<< "\n\toffset = " << infos.offset
						<< "\n\tfileNPieces = " << infos.nPieces
						<< std::endl;

						bitfield pieces = i->pieces;
						setFileInfos(infos.path, infos.fileSize, [=](long long off, long long size) -> long long {
								//Piece number of the start of $off in file
								int start = (off + infos.offset) / infos.pieceLength;

								long long res = 0;
								if(!pieces[start])
									return 0;

								// Since we have current piece, we can at least read what's left from current offset to the end of the piece
								res = infos.pieceLength - ( (off + infos.offset ) %infos.pieceLength);

								// Now count how many (full) pieces we can still get from here.
								int pos = start+1;
								while(res < size) {
									//We're beyond current file, stop here
									if(pos > infos.lastPiece)
										break;

									// Next piece isn't available, that's all we can read
									if(!pieces[pos])
										break;

									// If we arrived at the last piece, we managed to get all the pieces of the file, so we can read everything
									if(pos == infos.lastPiece) {
										res = infos.fileSize - off;
										break;
									}

									// Got one more piece to read
									res += infos.pieceLength;
									++pos;
								}

								if(res>size)
									res = size;
								return res;
							});
				}
			} else {
				std::cerr << alert->message() << std::endl;
			}
		}
	}

	end(0);
	return 0;
}

