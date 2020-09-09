#pragma once
#include <aegis.hpp>
#include "download/download.h"
#include "slow_flush.h"

#include <Windows.h>

#include <vector>

#define READ_STEPS 100
#define MAX_FILE_SIZE 8e6

#define NOW_T std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())

const std::string project_name = "AEB";
const std::string version = "V1.6.4.3";
const unsigned long long idd = 749852332321144863;
const unsigned long long mee_dev = 280450898885607425; // myself, for debugging and help

using namespace LSW::v5;


LONGLONG getFileSize(const std::string&);


struct D { // deleter
	void operator()(FILE* p) const {
		if (p) fclose(p);
		p = nullptr;
	};
};


struct custom_message_save {
	std::string timestamp;
	std::string username;
	std::string discriminator;
	std::string content;
	bool has_cleared_content_already = false;
	std::vector<std::pair<std::string, size_t>> reactions;
	std::vector<std::string> embeds_json;
	std::vector<std::pair<std::string, std::string>> attachments;

	custom_message_save() = default;
	custom_message_save(aegis::gateway::objects::message&);

	friend void from_json(const nlohmann::json& j, custom_message_save& m);
	friend void to_json(nlohmann::json& j, const custom_message_save& m);
};


class data_being_worked_on {
	std::shared_ptr<aegis::core> thebot;
	std::shared_ptr<spdlog::logger> logg;
	aegis::snowflake guildid;
	aegis::snowflake channel_from;
	aegis::snowflake channel_output;
	std::vector<aegis::snowflake> channels_to_save;

	std::chrono::milliseconds last_call = NOW_T,
		last_thousand = NOW_T;

	std::unique_ptr<FILE, D> last_step;
	std::string last_step_path;

	size_t failures = 0;
	bool thread_in_works = true;
	bool just_die = false;

	std::unordered_map<aegis::snowflake, std::pair<std::map<std::string, custom_message_save>, std::string>> in_order; // channel id, <content, channel_name>

	std::thread in_works;

	// 0 means no buffer, 1 means done once, 2 means skip, 3 means something wrong happened (no huge error).
	int get_messages_one_channel();

	// 0 means no buffer, 1 means done once, 2 means something wrong happened (no huge error), 3 means flush channel is impossible to get.
	int send_one_channels_messages();


	void work();

	std::chrono::milliseconds wait_for_auto(); // secure mode? fixed return 1005 ms

	aegis::channel* notif_ch();
	aegis::channel* save_ch();
	aegis::channel* specific_ch(aegis::snowflake);

	void flush_file();
	bool prepare_and_load_any_pendencies(bool);
public:
	data_being_worked_on(std::shared_ptr<aegis::core>, aegis::snowflake);
	data_being_worked_on(std::shared_ptr<aegis::core>, aegis::snowflake, aegis::channel&, aegis::snowflake, std::vector<aegis::snowflake>);

	~data_being_worked_on();

	void has_to_die_now_please_goodbye();

	bool done();
};

// if guildid has flush, should call data_being_worked_on to work on it
bool has_flush(aegis::snowflake);




/// \cond TEMPLATES
inline void from_json(const nlohmann::json& j, custom_message_save& m)
{
    if (j.count("timestamp") && !j["timestamp"].is_null())
        m.timestamp = j["timestamp"].get<std::string>();
    if (j.count("username") && !j["username"].is_null())
        m.username = j["username"].get<std::string>();
    if (j.count("discriminator") && !j["discriminator"].is_null())
        m.discriminator = j["discriminator"].get<std::string>();
    if (j.count("content") && !j["content"].is_null())
        m.content = j["content"].get<std::string>();
    if (j.count("has_cleared_content_already") && !j["has_cleared_content_already"].is_null())
        m.has_cleared_content_already = j["has_cleared_content_already"].get<bool>();

    if (j.count("reactions") && !j["reactions"].is_null())
        for (const auto& _field : j["reactions"])
            m.reactions.push_back(_field);
    if (j.count("embeds_json") && !j["embeds_json"].is_null())
        for (const auto& _field : j["embeds_json"])
            m.embeds_json.push_back(_field);
    if (j.count("attachments") && !j["attachments"].is_null())
        for (const auto& _field : j["attachments"])
            m.attachments.push_back(_field);
}

inline void to_json(nlohmann::json& j, const custom_message_save& m)
{
    j["timestamp"] = m.timestamp;
    j["username"] = m.username;
    j["discriminator"] = m.discriminator;
    j["content"] = m.content;
    j["has_cleared_content_already"] = m.has_cleared_content_already;

	for (const auto& _field : m.reactions)
		j["reactions"].push_back(_field);
	for (const auto& _field : m.embeds_json)
		j["embeds_json"].push_back(_field);
	for (const auto& _field : m.attachments)
		j["attachments"].push_back(_field);
}
/// \endcond