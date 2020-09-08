#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

LONGLONG getFileSize(const std::string& s)
{
	HANDLE hFile = CreateFile(s.c_str(), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return -1; // error condition, could call GetLastError to find out more

	LARGE_INTEGER size;
	if (!GetFileSizeEx(hFile, &size))
	{
		CloseHandle(hFile);
		return -1; // error condition, could call GetLastError to find out more
	}

	CloseHandle(hFile);
	return size.QuadPart;
}


int main(int argc, char* argv[])
{
	std::vector<std::string> strs;

	for (size_t p = 1; p < argc; p++) {
		if (getFileSize(argv[p])) {
			strs.push_back(argv[p]);
			printf_s("Added: %s\n", argv[p]);
		}
		else {
			printf_s("Invalid: %s\n", argv[p]);
		}
	}

	if (argc == 1) printf_s("What files you want to add? (CONTINUE to mash them into output)\n");
	else printf_s("Want to add more files? (CONTINUE to mash them into output)\n");
	
	while (1) {
		std::string buff;
		std::getline(std::cin, buff);

		if (buff == "CONTINUE") break;

		if (buff.length() > 1) {
			if (buff.front() == '\"') buff.erase(buff.begin());
			if (buff.back() == '\"') buff.pop_back();
		}

		if (getFileSize(buff)) {
			strs.push_back(buff);
			printf_s("Added: %s\n", buff.c_str());
		}
		else {
			printf_s("Invalid: %s\n", buff.c_str());
		}
	}

	std::string end_f = ".unknown";

	if (strs.size() == 0) {
		printf_s("No files were in the list.\n");
		int a;
		std::cin >> a;
	}

	if (size_t poss = strs[0].rfind('.'); poss != std::string::npos) {
		end_f = strs[0].substr(poss);
		for (auto& i : strs) {
			if (i.find(end_f) == std::string::npos) {
				end_f = ".unknown";
				break;
			}
		}
	}




	FILE* out = nullptr;
	if (fopen_s(&out, ("output" + end_f).c_str(), "wb") != 0) {
		printf_s("Failed to open file to save. Please try again somewhere else.\n");
		return 1;
	}

	size_t total_bytes = 0;

	for (auto& i : strs) {

		FILE* read = nullptr;
		if (fopen_s(&read, i.c_str(), "rb") != 0) {
			printf_s("Cannot read %s. Skipping...\n", i.c_str());
			continue;
		}
		else {
			printf_s("Working on %s...\n", i.c_str());
		}

		char megabuf[4096];
		size_t read_now = 0;

		while ((read_now = fread_s(megabuf, 4096, sizeof(char), 4096, read)) > 0)
		{
			total_bytes += fwrite(megabuf, sizeof(char), read_now, out);
			if (feof(read)) break;
		}

		fclose(read);
	}
	fclose(out);


	printf_s("Done (%zu bytes).\n", total_bytes);
	int a;
	std::cin >> a;
}