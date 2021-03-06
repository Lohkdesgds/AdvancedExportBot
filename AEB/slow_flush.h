#pragma once

#include <aegis.hpp>

/*
Best based on a T6400 Core 2 Duo slow machine: LSW_NO_GET only.
*/

// or this
//#define LSW_ALT_NO_GET_USE_THEN

// or
#define LSW_NO_GET
//#define LSW_DONTCAREMODE // can combine with NO_GET


#define LSW_MINWAIT_TIME 1000



const size_t max_tries = 2;
#ifdef LSW_MINWAIT_TIME
const std::chrono::milliseconds min_wait = std::chrono::milliseconds(LSW_MINWAIT_TIME);
#else
const std::chrono::milliseconds min_wait = std::chrono::milliseconds(0);
#endif
const std::chrono::milliseconds error_wait = std::chrono::milliseconds(800);
const size_t safe_msg_limit = 2000;
const size_t safe_file_size_limit = 8e6;

class slowflush_end : public aegis::gateway::objects::message {
	bool has_failed = false;
public:
	slowflush_end() = default;
	template<typename... Args>
	slowflush_end(Args... args) : aegis::gateway::objects::message(args...) {}

	slowflush_end& failed() {
		has_failed = true;
		return *this;
	}
	bool good() {
		return !has_failed;
	}
	operator bool() const {
		return !has_failed;
	}
};

//inline std::mutex slow_flush_control; // yes, sad vibes

slowflush_end slow_flush(aegis::create_message_t, aegis::channel&, unsigned long long, std::shared_ptr<spdlog::logger>);
slowflush_end slow_flush(aegis::rest::aegis_file, aegis::channel&, unsigned long long, std::shared_ptr<spdlog::logger>);
slowflush_end slow_flush(std::string, aegis::channel&, unsigned long long, std::shared_ptr<spdlog::logger>);

slowflush_end slow_flush_embed(nlohmann::json, aegis::channel&, unsigned long long, std::shared_ptr<spdlog::logger>);
slowflush_end slow_flush_embed(aegis::gateway::objects::embed, aegis::channel&, unsigned long long, std::shared_ptr<spdlog::logger>);