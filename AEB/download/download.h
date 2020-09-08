#pragma once

// C
#include <windows.h>
#include <wininet.h>
#include <ShlObj.h>
// C++
#include <string>
#include <thread>
#include <functional>

#pragma comment (lib, "wininet.lib")


namespace LSW {
	namespace v5 {
		namespace download {
			constexpr auto max_block_size = 8192;
		}

		class Downloader {
			std::string buf;
			std::thread thr;
			bool threadend = true;
			size_t TotalBytesRead = 0;
		public:
			bool get(const char*);

			bool getASync(const char*);
			bool ended();
			size_t bytesRead();

			std::string& read();
		};
	}
}