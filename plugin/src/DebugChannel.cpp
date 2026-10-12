#include "DebugChannel.h"
#include "Config.h"

#include <fstream>
#include <sstream>

void DebugChannel::Poll()
{
	if (!Config::telemetry) return;
	static const std::filesystem::path file{ "Data/SKSE/Plugins/TES64_debug.txt" };
	std::error_code ec;
	if (!std::filesystem::exists(file, ec)) return;

	std::vector<std::string> lines;
	{
		std::ifstream f(file);
		for (std::string line; std::getline(f, line);) lines.push_back(line);
	}
	std::filesystem::remove(file, ec);

	auto player = RE::PlayerCharacter::GetSingleton();
	for (auto& line : lines) {
		std::istringstream in(line);
		std::string        cmd, arg;
		in >> cmd >> arg;
		if (cmd == "equipshout") {
			auto shout = RE::TESForm::LookupByID<RE::TESShout>(static_cast<RE::FormID>(std::stoul(arg, nullptr, 16)));
			if (shout && player) RE::ActorEquipManager::GetSingleton()->EquipShout(player, shout);
			logger::info("debug: equipshout {} -> {}", arg, shout ? "ok" : "no such shout");
		} else if (!cmd.empty()) {
			logger::info("debug: unknown command '{}'", cmd);
		}
	}
}
