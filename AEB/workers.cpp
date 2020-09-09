#include "workers.h"

LONGLONG getFileSize(const std::string& s)
{
	HANDLE hFile = CreateFile(std::wstring(s.begin(), s.end()).c_str(), GENERIC_READ, // they are unibyte, no problem doing begin() and end()
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

custom_message_save::custom_message_save(aegis::gateway::objects::message& m)
{
	timestamp = m.timestamp;
	username = m.author.username;
	discriminator = m.author.discriminator;
	content = m.get_content();
	for (auto& i : m.reactions) {
		reactions.push_back({ i.emoji_.id != 0 ? ((i.emoji_.animated ? u8"<a:" : u8"<:") + i.emoji_.name + u8":" + std::to_string(i.emoji_.id) + u8">") : i.emoji_.name, i.count });
	}
	for (auto& i : m.embeds) {
		nlohmann::json jss;
		aegis::gateway::objects::to_json(jss, i);
		embeds_json.push_back(jss.dump());
	}
	for (auto& i : m.attachments) {
		attachments.push_back({ i.url, i.filename });
	}
}


int data_being_worked_on::get_messages_one_channel() {

	std::this_thread::yield();

	if (just_die) {
		logg->info("Guild #{} gave up on its task (emergency exit called).", guildid);
		return -1;
	}

	if (channels_to_save.size() == 0) return 0;
	if (in_order.size() > 1) return 1;

	auto& one = channels_to_save.front();

	logg->info("Searching channel #{} for reading...", one);
	std::this_thread::yield();
	if (auto* ch = notif_ch(); ch) if (!slow_flush("Searching channel #" + std::to_string(one) + " for reading...", *ch, guildid, thebot->log)) throw - 3;

	aegis::channel* this_ch = specific_ch(one);

	if (!this_ch) {
		if (++failures >= 10) {
			logg->info("Skipping channel #{} because of multiple fails (10).", one);

			if (auto* ch = notif_ch(); ch) if (!slow_flush("Skipping channel #" + std::to_string(one) + " because of multiple fails (10).", *ch, guildid, thebot->log)) throw - 3;

			std::this_thread::yield();

			channels_to_save.erase(channels_to_save.begin()); // can't

			flush_file();

			return 2;
		}
		return 3;
	}
	else failures = 0;

	logg->info("Got channel #{}.", one);
	std::this_thread::yield();
	if (auto* ch = notif_ch(); ch) if (!slow_flush("Found channel <#" + std::to_string(one) + ">. Reading channel...", *ch, guildid, thebot->log)) throw - 3;

	aegis::snowflake last_message_id = this_ch->get_last_message_id();

	aegis::get_messages_t message_get_formula;
	message_get_formula.message_id(last_message_id);
	message_get_formula.limit(1);
	message_get_formula.around();

	in_order[one].second = this_ch->get_name();

	for (size_t p = 0; p < 7; p++) {
		try {
			auto mmm = this_ch->get_messages(message_get_formula);
			while (!mmm.available()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
			auto messages = mmm.get();


			for (auto& o : messages._messages) {
				in_order[one].first[o.timestamp + std::to_string(o.get_id())] = o;
			}

			break;
		}
		catch (...) {
			logg->info("[{}/7][Local] Failed to get last message on channel #{}. No worries if it fails all the times.", p + 1, one);
			std::this_thread::yield();
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			std::this_thread::yield();
		}
	}

	message_get_formula.message_id(last_message_id);
	message_get_formula.limit(READ_STEPS);
	message_get_formula.before();

	aegis::gateway::objects::messages latest_messages;
	aegis::snowflake last_now = last_message_id;

	try {
		do {
			if (just_die) {
				logg->info("Guild #{} gave up on its task (emergency exit called).", guildid);
				return -1;
			}

			message_get_formula.message_id(last_now);
			latest_messages._messages.clear(); 

			auto mmm = this_ch->get_messages(message_get_formula);
			while (!mmm.available()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
			latest_messages = mmm.get();

			bool changed_once = false;

			for (auto& o : latest_messages._messages) {
				in_order[one].first[o.timestamp + std::to_string(o.get_id())] = o;

				auto kk = o.get_id();
				if (kk < last_now) {
					last_now = kk;
					changed_once = true;
				}
			}

			logg->info("[Local] Reading channel #{}... ({} messages in memory)", one, in_order[one].first.size());
			if (!changed_once) break;

		} while (latest_messages._messages.size() > 0);
	}
	catch (...) {
		logg->info("[Local] Something went wrong while reading channel #{}. Trying again...", one);
		return 3;
	}

	logg->info("Fully read channel #{}. ({} messages in memory)", one, in_order[one].first.size());
	std::this_thread::yield();
	if (auto* ch = notif_ch(); ch) if (!slow_flush("Fully read channel <#" + std::to_string(one) + ">. (" + std::to_string(in_order[one].first.size()) + " messages in memory)", *ch, guildid, thebot->log)) throw - 3;

	channels_to_save.erase(channels_to_save.begin()); // done

	flush_file();

	return 1;
}

int data_being_worked_on::send_one_channels_messages() {
	if (in_order.size() == 0) return 0;
	if (just_die) {
		logg->info("Guild #{} gave up on its task (emergency exit called).", guildid);
		return -1;
	}

	auto& i = *in_order.begin();

	logg->info("Flushing channel #{}... ({} message(s) and {} chat(s) remaining)", i.first, i.second.first.size(), (channels_to_save.size() + 1));
	if (auto* ch = notif_ch(); ch) if (!slow_flush("Flushing channel <#" + std::to_string(i.first) + "> (#" + std::to_string(i.first) + ", " + std::to_string(i.second.first.size()) + " message(s) and " + std::to_string(channels_to_save.size() + 1) + " chat(s) remaining)", *ch, guildid, thebot->log)) throw - 3;

	aegis::channel* copy_ch = nullptr;
	try {
		copy_ch = save_ch();
	}
	catch (...) {
		logg->info("Failed to get save chat on Guild #{}", guildid);
		return 2;
	}

	if (!copy_ch) {
		if (++failures >= 10) {
			logg->error("Aborting because the flush channel is not available anymore.");

			if (auto* ch = notif_ch(); ch) slow_flush("Aborting because the flush channel is not available anymore.", *ch, guildid, thebot->log);
			else throw 1;

			return 3;
		}
		return 2;
	}
	else failures = 0;

	logg->info("[Local] Got channel #{}.", channel_output);
	last_thousand = NOW_T;

	size_t calc_amount_per_t = i.second.first.size();

	while (i.second.first.size() > 0) {

		if (just_die) {
			logg->info("Guild #{} gave up on its task (emergency exit called).", guildid);
			return -1;
		}

		auto& j = *i.second.first.begin();
		std::string key = i.second.second;

		if (i.second.first.size() % 50 == 0 && i.second.first.size() > 0) {
			logg->info("Updating bot nickname on Guild #{}", guildid);
			try {
				auto u = thebot->guild_create(guildid, &thebot->get_shard_by_guild(guildid));
				if (i.second.first.size() / 50 == 1) u->modify_my_nick("AdvancedExportBot");
				else u->modify_my_nick("AEB - " + (i.second.first.size() > 10e9 ? ">= 10E9" : std::to_string(i.second.first.size())) + " in memory");
			}
			catch (aegis::error e) {
				static aegis::category one;
				logg->warn("Guild #{} failed to update nickname: {}", guildid, one.message(e));
			}
			catch (std::exception e) {
				logg->warn("Guild #{} failed to update nickname: {}", guildid, e.what());
			}
			catch (...) {
				logg->warn("Guild #{} failed to update nickname. Unknown error.", guildid);
			}
		}

		if (i.second.first.size() % 1000 == 0 && i.second.first.size() > 0) {
			logg->info("Updating bot ETA on Guild #{}", guildid);
			size_t prop = calc_amount_per_t - i.second.first.size(); // 1000 is the default.

			if (prop < 100) prop = 100;
			else if (prop > 1000) {
				logg->warn("Guild #{} got weird amount on last_size - size().", guildid);
				prop = 1000;
			}

			calc_amount_per_t = i.second.first.size();
			long long ss = ((NOW_T - last_thousand).count() / 1000); // seconds
			long long ss_to_go = ss * (i.second.first.size() / 1000.0) * (1000.0 / prop); // if there were 1000, 1x, if there were 500, 2x
			last_thousand = NOW_T;

			std::string end_str;
			if (ss > 86400) { // day(s)
				int days = ss / 86400;
				ss %= 86400;
				end_str += (end_str.length() ? " " : "") + std::to_string(days) + (days > 1 ? " days" : " day");
			}
			if (ss > 3600) { // hour(s)
				int hours = ss / 3600;
				ss %= 3600;
				end_str += (end_str.length() ? " " : "") + std::to_string(hours) + (hours > 1 ? " hours" : " hour");
			}
			if (ss > 60) { // minute(s)
				int minutes = ss / 60;
				ss %= 60;
				end_str += (end_str.length() ? " " : "") + std::to_string(minutes) + (minutes > 1 ? " minutes" : " minute");

			}
			if (ss) { // second(s)
				end_str += (end_str.length() ? " " : "") + std::to_string(ss) + (ss > 1 ? " seconds" : " second");
			}

			std::string to_end;
			if (ss_to_go > 86400) { // day(s)
				int days = ss_to_go / 86400;
				ss_to_go %= 86400;
				to_end += (to_end.length() ? " " : "") + std::to_string(days) + (days > 1 ? " days" : " day");
			}
			if (ss_to_go > 3600) { // hour(s)
				int hours = ss_to_go / 3600;
				ss_to_go %= 3600;
				to_end += (to_end.length() ? " " : "") + std::to_string(hours) + (hours > 1 ? " hours" : " hour");
			}
			if (ss_to_go > 60) { // minute(s)
				int minutes = ss_to_go / 60;
				ss_to_go %= 60;
				to_end += (to_end.length() ? " " : "") + std::to_string(minutes) + (minutes > 1 ? " minutes" : " minute");

			}
			if (ss_to_go) { // second(s)
				to_end += (to_end.length() ? " " : "") + std::to_string(ss_to_go) + (ss_to_go > 1 ? " seconds" : " second");
			}



			logg->info("#{} has {} messages remaining now. Last message took: {}. ETA: {}", i.first, i.second.first.size(), end_str, to_end);
			if (auto* ch = notif_ch(); ch) if (!slow_flush(std::to_string(i.second.first.size()) + " messages to go. Time taken (" + std::to_string(prop) + " message(s)): " + end_str + ". Estimated time for this chat (based on this): " + to_end + ".", *ch, guildid, thebot->log)) throw - 3;
		}

		{
			custom_message_save& k = j.second;

			// - - - - - - - - - - > CONTENT < - - - - - - - - - - //

			if (!k.has_cleared_content_already) {
				aegis::create_message_t msg;
				std::string calculated = u8"```md\n[" + k.timestamp + u8"](" + key + u8")<" + k.username + "#" + k.discriminator + u8">: " + (k.content.find("```") == std::string::npos ? (k.content + "```") : ("```\n" + k.content));

				if (calculated.length() <= 2000) { // <= 2000 is fine
					msg.content(calculated);
					if (!slow_flush(msg, *copy_ch, guildid, thebot->log)) throw - 3;
					k.has_cleared_content_already = true;
					flush_file();

					std::this_thread::yield();
					std::this_thread::sleep_for(wait_for_auto());
					std::this_thread::yield();
				}
				else { // split if more
					msg.content(u8"```md\n[" + k.timestamp + u8"](" + key + u8")<" + k.username + "#" + k.discriminator + u8"> sent huge block:```"); // send message content itself just to be sure 2000 chars are cool
					if (!slow_flush(msg, *copy_ch, guildid, thebot->log)) throw - 3;

					if (!slow_flush(aegis::create_message_t().content(k.content), *copy_ch, guildid, thebot->log)) throw - 3;
					k.has_cleared_content_already = true;
					flush_file();

					std::this_thread::yield();
					std::this_thread::sleep_for(wait_for_auto());
					std::this_thread::yield();
				}
			}

			// - - - - - - - - - - > REACTIONS < - - - - - - - - - - //

			while (k.reactions.size() > 0) {
				auto& i = k.reactions.front();
				aegis::create_message_t msg2;
				//std::string autoemoji = i.emoji_.id != 0 ? ((i.emoji_.animated ? u8"<a:" : u8"<:") + i.emoji_.name + u8":" + std::to_string(i.emoji_.id) + u8">") : i.emoji_.name;
				//msg2.content(u8"` ⇒ has react: " + autoemoji + " x" + std::to_string(i.count) + "`");
				msg2.content(u8"` ⇒ has react: " + i.first + " x" + std::to_string(i.second) + "`");
				if (!slow_flush(msg2, *copy_ch, guildid, thebot->log)) throw - 3;

				k.reactions.erase(k.reactions.begin());
				flush_file();

				std::this_thread::yield();
				std::this_thread::sleep_for(wait_for_auto());
				std::this_thread::yield();
			}

			// - - - - - - - - - - > EMBEDS < - - - - - - - - - - //

			while (k.embeds_json.size() > 0) {
				auto& i = k.embeds_json.front();
				if (!slow_flush_embed(nlohmann::json::parse(i), *copy_ch, guildid, thebot->log)) {
					std::string bus = "Impossible to load embed: " + i;
					if (bus.length() > 2000) {
						bus = bus.substr(0, 1997) + "...";
					}
					if (!slow_flush(bus, *copy_ch, guildid, thebot->log)) throw -3;
				}

				k.embeds_json.erase(k.embeds_json.begin());
				flush_file();

				std::this_thread::yield();
				std::this_thread::sleep_for(wait_for_auto());
				std::this_thread::yield();
			}

			// - - - - - - - - - - > FILES < - - - - - - - - - - //

			while (k.attachments.size() > 0) {
				auto& i = k.attachments.front();
				aegis::create_message_t msg2;
				Downloader down;
				down.getASync(i.first.c_str());

				while (!down.ended()) {
					std::this_thread::yield();
					std::this_thread::sleep_for(std::chrono::milliseconds(300));
				}

				logg->info("Downloaded {}.", i.second);

				if (down.read().size() > MAX_FILE_SIZE) {
					if (!slow_flush("`Had to split file. This is the RAW data split in " + std::to_string((int)(1 + (down.read().size() - 1) / MAX_FILE_SIZE)) + " slices`.", *copy_ch, guildid, thebot->log)) throw -3;

					std::this_thread::yield();
					std::this_thread::sleep_for(wait_for_auto());
					std::this_thread::yield();
				}

				for (size_t cuts = 0; cuts <= ((down.read().size() == 0 ? 0 : down.read().size() - 1) / MAX_FILE_SIZE); cuts++) { // if 8000, goes once.
					aegis::rest::aegis_file fp;
					for (size_t p = cuts * MAX_FILE_SIZE; p < (cuts + 1) * MAX_FILE_SIZE && p < down.read().size(); p++) fp.data.push_back(down.read()[p]);
					if (fp.data.size() == 0) break;
					fp.name = i.second;

					msg2.file(fp);
					msg2.content(i.second);

					if (!slow_flush(msg2, *copy_ch, guildid, thebot->log)) throw -3;

					std::this_thread::yield();
					std::this_thread::sleep_for(wait_for_auto());
					std::this_thread::yield();
				}

				k.attachments.erase(k.attachments.begin());
				flush_file();
			}
		}

		i.second.first.erase(i.second.first.begin());
		flush_file();
	}

	in_order.erase(in_order.begin());
	flush_file();

	return 1;
}

void data_being_worked_on::work() {
	if (just_die) return;

	bool got_done = false;
	while (!got_done) {
		try {
			try {
				bool doing = true;

				auto send = [&] {
					switch (send_one_channels_messages()) {
					case 0: // just in case, dunno.
						doing = false;
						break;
					case 1:
						logg->info("Done flushing this chat.");
						if (auto* ch = notif_ch(); ch) { if (!slow_flush("Done flushing this chat.", *ch, guildid, thebot->log)) throw - 3; }
						else throw 1;

						std::this_thread::yield();
						std::this_thread::sleep_for(wait_for_auto());
						std::this_thread::yield();

						break;
					case 2:
						logg->info("Something went wrong while flushing the list. Trying again in 10 seconds.");
						if (auto* ch = notif_ch(); ch) { if (!slow_flush("Something went wrong while flushing the list. Trying again in 10 seconds.", *ch, guildid, thebot->log)) throw - 3; }
						else throw 1;

						std::this_thread::yield();
						std::this_thread::sleep_for(std::chrono::seconds(10));
						std::this_thread::yield();
						break;
					case 3:
						logg->info("Sorry, but I cannot find the flush channel anymore. You should try again.");
						if (auto* ch = notif_ch(); ch) { if (!slow_flush("Sorry, but I cannot find the flush channel anymore. You should try again.", *ch, guildid, thebot->log)) throw - 3; }
						else throw 1;

						std::this_thread::yield();

						break;
						// -1 (just_die) just dies...
					}
				};

				auto get_and_send = [&] {
					int answr = get_messages_one_channel();
					if (just_die) {
						logg->warn("[Local] Guild #{} got emergency die call. Ending now.", guildid);
						logg->info("[Local] Guild #{} ended all tasks.", guildid);
					}
					switch (answr) {
					case 0:
						doing = in_order.size();
					case 1:
						logg->info("Done reading this chat.");
						if (auto* ch = notif_ch(); ch) { if (!slow_flush("Done reading this chat.", *ch, guildid, thebot->log)) throw - 3; }

						std::this_thread::yield();
						std::this_thread::sleep_for(wait_for_auto());
						std::this_thread::yield();

						while (in_order.size() > 0 && !just_die) { // once get_message is done, even with crashes, it can flush everything
							logg->info("Preparing to flush a chat on Guild #{}", guildid);
							send();
							if (just_die) {
								logg->warn("[Local] Guild #{} got emergency die call. Ending now.", guildid);
								logg->info("[Local] Guild #{} ended all tasks.", guildid);
							}
							else logg->info("Fully flushed a chat on Guild #{}", guildid);
						}
						break;
					case 2:
						logg->warn("[{}/10] Couldn't get channel's messages. Trying again in 10 seconds.", failures);
						if (auto* ch = notif_ch(); ch) { if (!slow_flush("Couldn't get channel's messages. Trying again in 10 seconds.", *ch, guildid, thebot->log)) throw - 3; }
						else throw 1;

						std::this_thread::yield();
						std::this_thread::sleep_for(std::chrono::seconds(10));
						std::this_thread::yield();

						break;
					case 3:
						logg->warn("Something went wrong while creating the list. Trying again in 10 seconds.");
						if (auto* ch = notif_ch(); ch) { if (!slow_flush("Something went wrong while creating the list. Trying again in 10 seconds.", *ch, guildid, thebot->log)) throw - 3; }
						else throw 1;

						std::this_thread::yield();
						std::this_thread::sleep_for(std::chrono::seconds(10));
						std::this_thread::yield();

						break;
					default: // use -1
						if (just_die) {
							logg->warn("[Local] Guild #{} got emergency die call. Ending now.", guildid);
							logg->info("[Local] Guild #{} ended all tasks.", guildid);
						}
						return;
					}
				};

				while (doing) {
					if (just_die) {
						thread_in_works = false;
						return;
					}

					if (just_die) {
						thread_in_works = false;
						return;
					}

					get_and_send(); // contains send() once
				}

				if (just_die) {
					thread_in_works = false;
					return;
				}
			}
			catch (...) {
				if (just_die) {
					thread_in_works = false;
					return;
				}
				logg->error("[Local] Something went wrong. Trying again in 20 seconds.");

				std::this_thread::yield();
				std::this_thread::sleep_for(std::chrono::seconds(20));
				std::this_thread::yield();
				continue;
			}
			got_done = true;
		}
		catch (...) {
			if (just_die) {
				thread_in_works = false;
				return;
			}
			logg->error("[Local] Something went wrong... Bot issues? Trying again in 20 seconds...");
			std::this_thread::yield();
			std::this_thread::sleep_for(std::chrono::seconds(20));
			std::this_thread::yield();
		}
	}


	logg->info("Ended tasks successfully. Cleaning up...");
	if (auto* ch = notif_ch(); ch) slow_flush("Ended tasks successfully.", *ch, guildid, thebot->log);

	std::this_thread::yield();
	std::this_thread::sleep_for(wait_for_auto());
	std::this_thread::yield();

	if (last_step) {
		last_step.reset();
	}
	std::remove(last_step_path.c_str());

	logg->info("Cleaned up.");

	try {
		auto u = thebot->guild_create(guildid, &thebot->get_shard_by_guild(guildid));
		u->modify_my_nick("AdvancedExportBot");
	}
	catch (aegis::error e) {
		static aegis::category one;
		logg->warn("Guild #{} failed to update nickname: {}", guildid, one.message(e));
	}
	catch (std::exception e) {
		logg->warn("Guild #{} failed to update nickname: {}", guildid, e.what());
	}
	catch (...) {
		logg->warn("Guild #{} failed to update nickname. Unknown error.", guildid);
	}

	thread_in_works = false;
}

std::chrono::milliseconds data_being_worked_on::wait_for_auto()
{
	auto diff = (std::chrono::milliseconds(1230) - (NOW_T - last_call));
	last_call = NOW_T;
	
	if (diff.count() < -3000) logg->warn("Hiccup at Guild #{}", guildid);


	std::cout << "T@" << (fabs(diff.count()) > 4000 ? (std::to_string((int)(diff.count() / 1000)) + " s") : (std::to_string(diff.count()) + " ms")) << "   \r";

	if (diff.count() < 850) {
		diff = std::chrono::milliseconds(850);
	}
	else if (diff.count() > 1050) {
		//if (diff.count() < 0) logg->info("Got bizarre time on guild #" << guildid << " of " << diff.count() << " ms. Doing 1000 ms instead.");
		diff = std::chrono::milliseconds(1050);
	}
	//logg->info("Guild #{} relaxing {} ms...", guildid, diff.count());

	return diff;
}


aegis::channel* data_being_worked_on::notif_ch() {
	return thebot->guild_create(guildid, &thebot->get_shard_by_guild(guildid))->get_channel(channel_from);
}
aegis::channel* data_being_worked_on::save_ch() {
	return thebot->guild_create(guildid, &thebot->get_shard_by_guild(guildid))->get_channel(channel_output);
}
aegis::channel* data_being_worked_on::specific_ch(aegis::snowflake ch) {
	return thebot->guild_create(guildid, &thebot->get_shard_by_guild(guildid))->get_channel(ch);
}

void data_being_worked_on::flush_file()
{
	static std::mutex m;

	if (!last_step) {
		logg->error("[!] Unsolvable task in Guild #{}. Please restart.", guildid);
		return;
	}
	//logg->info("[!] Guild #{} is about to flush its file...", guildid);

	std::lock_guard locking(m); // only one thread saving at a time just to be sure, I guess...

	fseek(last_step.get(), 0, SEEK_SET);
	
	nlohmann::json hmm;
	//hmm["in_order"] = in_order;

	for (auto& i : in_order) {
		hmm["in_order"].push_back(i);
	}


	hmm["channels_to_save"] = channels_to_save;
	hmm["channel_output"] = channel_output;
	hmm["channel_from"] = channel_from;

	auto dumped = hmm.dump() + u8"□□□□□LSW□□□□□";

	fwrite(dumped.c_str(), sizeof(char), dumped.length(), last_step.get());

	bool once_fail = false;

	while (fflush(last_step.get()) != 0) {
		once_fail = true;
		logg->info("[!] Guild #{} failed to flush file: ERRCODE = {}", guildid, ferror(last_step.get()));
		logg->info("[!] Guild #{} trying to reopen the file...", guildid);
		last_step.reset();

		FILE* novafile = nullptr;
		while (fopen_s(&novafile, last_step_path.c_str(), "rb+") != 0) {
			logg->info("[!] Guild #{} CAN'T OPEN FILE! Trying again in 10 seconds...", guildid);
			std::this_thread::yield();
			std::this_thread::sleep_for(std::chrono::seconds(10));
			std::this_thread::yield();
		}
		logg->info("[!] Guild #{} reopened the file. Saving...", guildid);

		last_step.reset(novafile);
		fwrite(dumped.c_str(), sizeof(char), dumped.length(), last_step.get());
	}

	if (once_fail) logg->info("[!] Guild #{} finally flushed file.", guildid);
}

bool data_being_worked_on::prepare_and_load_any_pendencies(bool create)
{
	if (last_step) return true;

	CreateDirectoryA("AEB", NULL);

	last_step_path = ("AEB/" + std::to_string(guildid) + ".work");

	FILE* novafile = nullptr;
	if (!create && fopen_s(&novafile, last_step_path.c_str(), "rb+") == 0) {
		last_step.reset(novafile);

		std::string data;
		char buf[4096];
		size_t siz = 0;
		while ((siz = fread_s(buf, 4096, sizeof(char), 4096, last_step.get())) > 0) {
			for (size_t h = 0; h < siz; h++) data += buf[h];
			if (feof(last_step.get())) break;
		}

		if (size_t pp = data.find(u8"□□□□□LSW□□□□□"); pp != std::string::npos) {
			data = data.substr(0, pp);
		}

		nlohmann::json hmm = nlohmann::json::parse(data);

		logg->warn("[!] Got not done task from Guild #{}", guildid);

		try {
			if (hmm.count("in_order") && !hmm["in_order"].is_null()) {
				this->in_order = hmm["in_order"].get<std::unordered_map<aegis::snowflake, std::pair<std::map<std::string, custom_message_save>, std::string>>>();
			}
			if (hmm.count("channels_to_save") && !hmm["channels_to_save"].is_null()) {
				this->channels_to_save = hmm["channels_to_save"].get<std::vector<aegis::snowflake>>();
			}
			if (hmm.count("channel_output") && !hmm["channel_output"].is_null()) {
				this->channel_output = hmm["channel_output"].get<aegis::snowflake>();
			}
			if (hmm.count("channel_from") && !hmm["channel_from"].is_null()) {
				this->channel_from = hmm["channel_from"].get<aegis::snowflake>();
			}
		}
		catch (std::exception e) {
			logg->error("[!] Failed to read saved data from Guild #{}: {}", guildid, e.what());
			return false;
		}
		catch (...) {
			logg->error("[!] Failed to read saved data from Guild #{}.");
			return false;
		}
		return true;
	}
	else if (create) {
		if (fopen_s(&novafile, last_step_path.c_str(), "wb") == 0) {
			last_step.reset(novafile);

			flush_file();

			logg->info("[!] Prepared file for Guild #{}", guildid);

			return true;
		}
		else {
			logg->info("[!] FATAL ERROR Guild #{} - could not create work file!", guildid);
			return false;
		}
	}
	return false;
}

data_being_worked_on::data_being_worked_on(std::shared_ptr<aegis::core> thebot, aegis::snowflake guildid)
{
	this->thebot = thebot;
	this->logg = thebot->log;
	this->guildid = guildid;

	if (!prepare_and_load_any_pendencies(false)) {
		logg->error("[!] FATAL ERROR Guild #{} CANNOT LOAD LATEST WORK!", guildid);
	}
	else {
		thread_in_works = true;
		if (auto* ch = notif_ch(); ch) slow_flush("Sorry, something went wrong (probably internet fluctuation or my pc is weird), but I'm trying to work on it...", *ch, guildid, thebot->log);
		in_works = std::thread([&] {work(); });
	}
}

data_being_worked_on::data_being_worked_on(std::shared_ptr<aegis::core> thebot, aegis::snowflake guildid, aegis::channel& ch_from, aegis::snowflake save_where, std::vector<aegis::snowflake> channels_to_save) {
	this->thebot = thebot;
	this->logg = thebot->log;
	this->guildid = guildid;
	this->channel_from = ch_from.get_id();
	this->channel_output = save_where;
	this->channels_to_save = channels_to_save;

	if (!prepare_and_load_any_pendencies(true)) {
		logg->info("[!] FATAL ERROR Guild #{} - could not create work file!", guildid);
		exit(EXIT_FAILURE);
	}

	thread_in_works = true;
	in_works = std::thread([&] {work(); });
}

data_being_worked_on::~data_being_worked_on() {
	logg->info("[!] Closing Guild #{}...", guildid);
	if (in_works.joinable()) in_works.join();

	try {
		auto u = thebot->guild_create(guildid, &thebot->get_shard_by_guild(guildid));
		u->modify_my_nick("AdvancedExportBot");
	}
	catch (aegis::error e) {
		static aegis::category one;
		logg->warn("Guild #{} failed to update nickname: {}", guildid, one.message(e));
	}
	catch (std::exception e) {
		logg->warn("Guild #{} failed to update nickname: {}", guildid, e.what());
	}
	catch (...) {
		logg->warn("Guild #{} failed to update nickname. Unknown error.", guildid);
	}

	logg->info("[!] Closed Guild #{}.", guildid);
}

void data_being_worked_on::has_to_die_now_please_goodbye()
{
	logg->info("[!] Guild #{} got STOP call. Set to end soon.", guildid);
	just_die = true;
	//if (in_works.joinable()) in_works.join();
}

bool data_being_worked_on::done() {
	return !thread_in_works;
}

bool has_flush(aegis::snowflake guildid) {
	return getFileSize("AEB/" + std::to_string(guildid) + ".work") > 0;
}