#define WIN32_LEAN_AND_MEAN
#include "workers.h"
#include "keys.h"
#include <aegis.hpp>



int main()
{
	std::set_terminate([] {std::cout << "std::terminate() called before any handleAbort."; std::abort(); });
	signal(SIGABRT, [](int e){std::cout << "abort() called with code #" << e; std::abort(); });
	signal(SIGTERM, [](int e){std::cout << "termination request called with code #" << e; std::abort(); });
	signal(SIGSEGV, [](int e){std::cout << "invalid memory access called with code #" << e; std::abort(); });
	signal(SIGINT, [](int e){std::cout << "external interrupt called with code #" << e; std::abort(); });
	signal(SIGILL, [](int e){std::cout << "invalid instruction called with code #" << e; std::abort(); });
	signal(SIGFPE, [](int e){std::cout << "erroneous arithmetic called with code #" << e; std::abort(); });

	std::vector<std::shared_ptr<data_being_worked_on>> workers;
	std::mutex workers_m;

	std::shared_ptr<aegis::core> thebot;

	thebot = std::shared_ptr<aegis::core>(new aegis::core(aegis::create_bot_t().log_level(spdlog::level::trace).token(token)), [](aegis::core* c) {
		c->shutdown();
		delete c;
	});

	auto logg = thebot->log;
	
	// join in guild
	thebot->set_on_guild_create([&](aegis::gateway::events::guild_create obj) {
		logg->info("[!] Joined/Connected Guild {} #{} from {}", obj.guild.name, obj.guild.id, obj.guild.region);
		if (has_flush(obj.guild.id)) {
			logg->warn("[!] Detected pendencies on Guild {} #{} from {}", obj.guild.name, obj.guild.id, obj.guild.region);

			std::lock_guard gurd(workers_m);
			workers.emplace_back(std::move(std::make_shared<data_being_worked_on>(thebot, obj.guild.id)));
		}
	});
	thebot->set_on_guild_delete([&](aegis::gateway::events::guild_delete obj) {

		std::lock_guard gurd(workers_m);
		for (size_t p = 0; p < workers.size(); p++) {
			if (workers[p]->is_guild(obj.guild_id)) {
				workers[p]->has_to_die_now_please_goodbye();
				workers.erase(workers.begin() + p);
				logg->info("[!] Removed Guild #{}", obj.guild_id);
				return;
			}
		}
		
		
	});
	thebot->set_on_message_create([&](aegis::gateway::events::message_create obj) {
		if (obj.get_user().is_bot()) return;
		if (unsigned long long ownr = obj.msg.get_guild().get_owner(); ownr != obj.msg.author.id || ownr != mee_dev) return;

		if (auto msgg = obj.msg.get_content(); msgg.find("lsw/aeb") == 0) {
			auto default_help = [&] {
				slow_flush("Use: `lsw/aeb [ (backup <to> [<from> ...]) | refresh ]`", obj.channel, obj.msg.get_guild_id(), thebot->log);
			};

			if (msgg.find("lsw/aeb refresh") == 0) {
				auto guildid = obj.msg.get_guild().get_id();
				if (has_flush(guildid)) {
					slow_flush("Got work to do. Loading.", obj.channel, obj.msg.get_guild_id(), thebot->log);
					std::lock_guard gurd(workers_m);
					workers.emplace_back(std::move(std::make_shared<data_being_worked_on>(thebot, guildid)));
				}
			}
			else if (msgg.find("lsw/aeb backup ") == 0) {
				if (msgg.length() < std::string("lsw/aeb backup ").length()) {
					default_help();
					return;
				}

				auto slice = msgg.substr(std::string("lsw/aeb backup ").length());

				unsigned long long chat_to_save = 0;
				std::vector<aegis::snowflake> chats_to_read;

				std::vector<std::string> arguments;
				{
					std::string buf;
					for (size_t p = 0; p < slice.length(); p++) {
						auto& i = slice[p];
						if (i != ' ') buf += slice[p];
						else arguments.push_back(std::move(buf));
					}
					if (!buf.empty()) arguments.push_back(std::move(buf));
				}

				if (arguments.size() < 2) {
					default_help();
					return;
				}
				else {
					for (size_t pp = 0; pp < arguments.size(); pp++) {
						auto& k = arguments[pp];

						unsigned long long chatid = 0;
						std::string cpyy = k;
						while (cpyy.length() > 0 && (cpyy[0] < '0' || cpyy[0] > '9')) cpyy.erase(cpyy.begin());

						if (sscanf_s(cpyy.c_str(), "%llu", &chatid)) {
							if (pp == 0) chat_to_save = chatid;
							else chats_to_read.push_back(chatid);
							continue;
						}

						obj.channel.create_message("Can't find chat " + k);
						return;
					}
					logg->info("[!] Guild #{} (User: {}#{} #{}) called for backup.", obj.msg.get_guild_id(), obj.msg.author.username, obj.msg.author.discriminator, obj.msg.author.id);
					logg->info("[!] Guild #{}: Channels being saved: {}", obj.msg.get_guild_id(), chats_to_read.size());

					for (size_t ppp = 0; ppp < workers.size(); ppp++) {
						if (workers[ppp]->done()) {
							workers.erase(workers.begin() + ppp);
							ppp--;
						}
					}
					std::lock_guard gurd(workers_m);
					workers.emplace_back(std::move(std::make_shared<data_being_worked_on>(thebot, obj.msg.get_guild_id(), obj.msg.get_channel(), chat_to_save, chats_to_read)));
				}
			}
			else {
				default_help();
			}
		}
	});
	

	thebot->run();
	bool ignore_all_ending_lmao = false;

	std::thread here_lol = std::thread([&] {

		std::this_thread::sleep_for(std::chrono::seconds(5));

		auto keep = [&] {return (!ignore_all_ending_lmao); };

		while (keep()) {
			thebot->update_presence("lsw/aeb - " + version, aegis::gateway::objects::activity::Game, aegis::gateway::objects::presence::DoNotDisturb);
			for (size_t c = 0; c < 50 && keep(); c++) {
				std::this_thread::yield();
				std::this_thread::sleep_for(std::chrono::seconds(6));
			}
		}

	});

	std::thread here_lol2 = std::thread([&] {

		std::this_thread::sleep_for(std::chrono::seconds(5));

		auto keep = [&] {return (!ignore_all_ending_lmao); };

		while (keep()) {

			for (size_t c = 0; c < 8 && keep(); c++) {
				std::this_thread::yield();
				std::this_thread::sleep_for(std::chrono::seconds(2));
			}
			std::lock_guard gurd(workers_m);

			bool got_one = false;

			for (size_t p = 0; p < workers.size(); p++) {
				if (workers[p]->done())
				{
					logg->info("[!] One worker got the job done.");
					workers.erase(workers.begin() + p--);
					got_one = true;
				}
			}
			
			if (got_one) {
				logg->info("[!] Cleanup job tasked.");
			}
		}

	});


	std::string smth;
	while (smth != "exit") std::cin >> smth;
	ignore_all_ending_lmao = true;

	{
		std::lock_guard gurd(workers_m);
		for (auto& i : workers) i->has_to_die_now_please_goodbye();
		workers.clear();
	}

	logg->info("[!] Ended bot entirely.");

	thebot->shutdown();
	if (here_lol2.joinable()) here_lol2.join();
}