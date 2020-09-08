#include "download.h"

namespace LSW {
	namespace v5 {
			bool Downloader::get(const char* url)
			{
				HINTERNET connect = InternetOpen(L"LSW Downloader V5.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
				if (!connect) return false;
				HINTERNET OpenAddress = InternetOpenUrlA(connect, url, NULL, 0, INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_KEEP_CONNECTION, 0);
				if (!OpenAddress) return false;
				

				char DataReceived[download::max_block_size];
				DWORD NumberOfBytesRead = 0;
				TotalBytesRead = 0;

				buf.clear();

				buf.resize(download::max_block_size); // set maximum size

				while (InternetReadFile(OpenAddress, DataReceived, download::max_block_size, &NumberOfBytesRead) && NumberOfBytesRead) {
					buf.resize(TotalBytesRead + NumberOfBytesRead);
					memcpy_s(buf.data() + TotalBytesRead, NumberOfBytesRead, DataReceived, NumberOfBytesRead);
					TotalBytesRead += NumberOfBytesRead;
				}

				InternetCloseHandle(OpenAddress);
				InternetCloseHandle(connect);
				return true;
			}
			bool Downloader::getASync(const char* url)
			{
				if (!threadend) return false;
				threadend = false;
				thr = std::thread([&, url] { get(url); threadend = true; });
				return true;
			}
			bool Downloader::ended()
			{
				if (threadend) {
					if (thr.joinable()) thr.join();
				}
				return threadend;
			}
			size_t Downloader::bytesRead()
			{
				return TotalBytesRead;
			}
			std::string& Downloader::read()
			{
				return buf;
			}
	}
}
