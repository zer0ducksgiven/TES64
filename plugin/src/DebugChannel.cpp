#include "DebugChannel.h"
#include "Collision.h"
#include "Config.h"
#include "Hud.h"
#include "Pose.h"
#include "Reactions.h"
#include "Star.h"

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
		} else if (cmd == "cast") {
			// the player casts it on themselves, through the game's own magic caster
			auto spell = RE::TESForm::LookupByID<RE::SpellItem>(static_cast<RE::FormID>(std::stoul(arg, nullptr, 16)));
			auto caster = player ? player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr;
			if (spell && caster) caster->CastSpellImmediate(spell, false, player, 1.0f, false, 0.0f, player);
			logger::info("debug: cast {} -> {}", arg, spell && caster ? "ok" : "no such spell");
		} else if (cmd == "doors") {
			// every door within 6000 units: form id, position, open state (the harness walks Mario into one)
			if (auto tes = RE::TES::GetSingleton(); tes && player) {
				tes->ForEachReferenceInRange(player, 6000.0f, [&](RE::TESObjectREFR* ref) {
					auto base = ref->GetBaseObject();
					if (base && base->Is(RE::FormType::Door)) {
						auto p = ref->GetPosition();
						logger::info("DOOR {:08X} ({:.0f},{:.0f},{:.0f}) open={}", ref->GetFormID(), p.x, p.y, p.z,
							RE::BGSOpenCloseForm::GetOpenState(ref) == RE::BGSOpenCloseForm::OPEN_STATE::kOpen ? 1 : 0);
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
		} else if (cmd == "doorbodies") {
			Collision::LogDoorBodies(player, 6000.0f);
		} else if (cmd == "gmst") {
			// game settings whose name has arg in it (case-insensitive), with their values
			if (auto gs = RE::GameSettingCollection::GetSingleton()) {
				std::string want = arg;
				std::transform(want.begin(), want.end(), want.begin(), ::tolower);
				for (auto& item : gs->settings) {
					auto setting = item.second;
					if (!setting || !setting->GetName()) continue;
					std::string n = setting->GetName();
					std::transform(n.begin(), n.end(), n.begin(), ::tolower);
					if (n.find(want) == std::string::npos) continue;
					const auto type = setting->GetType();
					logger::info("GMST {} = {}", std::string(setting->GetName()), type == RE::Setting::Type::kFloat ? std::to_string(setting->GetFloat()) : type == RE::Setting::Type::kInteger ? std::to_string(setting->GetInteger()) : "?");
				}
			}
		} else if (cmd == "killnearest") {
			// the nearest living actor gets arg gold and dies (the coins test)
			RE::Actor* best = nullptr;
			float      bestD = 1e9f;
			if (auto tes = RE::TES::GetSingleton(); tes && player) {
				tes->ForEachReferenceInRange(player, 3000.0f, [&](RE::TESObjectREFR* ref) {
					auto a = ref->As<RE::Actor>();
					if (a && a != player && !a->IsDead()) {
						const float d = a->GetPosition().GetDistance(player->GetPosition());
						if (d < bestD) { bestD = d; best = a; }
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			auto gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0xF);
			if (best && gold) {
				best->AddObjectToContainer(gold, nullptr, arg.empty() ? 30 : std::stoi(arg), nullptr);
				best->KillImmediate();
			}
			logger::info("debug: killnearest -> {}", best ? fmt::format("{:08X} at {:.0f}", best->GetFormID(), bestD) : "nobody");
		} else if (cmd == "furniture") {
			// furniture refs within arg units of the player: form id, base, place, and the player's sit state
			if (auto tes = RE::TES::GetSingleton(); tes && player) {
				const float radius = arg.empty() ? 1500.0f : std::stof(arg);
				tes->ForEachReferenceInRange(player, radius, [&](RE::TESObjectREFR* ref) {
					auto base = ref->GetBaseObject();
					if (base && base->Is(RE::FormType::Furniture)) {
						auto p = ref->GetPosition();
						logger::info("FURN {:08X} base={:08X} ({:.0f},{:.0f},{:.0f}) dist={:.0f}", ref->GetFormID(), base->GetFormID(), p.x, p.y, p.z, p.GetDistance(player->GetPosition()));
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			logger::info("FURN player sit={} furniture={:08X} killmove={}", static_cast<int>(player->AsActorState()->GetSitSleepState()),
				player->GetOccupiedFurniture().get() ? player->GetOccupiedFurniture().get()->GetFormID() : 0, player->IsInKillMove() ? 1 : 0);
		} else if (cmd == "actors") {
			// actors within arg units of the player (form id, name, health), nearest first
			if (auto tes = RE::TES::GetSingleton(); tes && player) {
				const float radius = arg.empty() ? 1500.0f : std::stof(arg);
				tes->ForEachReferenceInRange(player, radius, [&](RE::TESObjectREFR* ref) {
					if (auto a = ref->As<RE::Actor>(); a && a != player) {
						const auto ap = a->GetPosition();
						logger::info("ACTOR {:08X} {} health={:.0f} dead={} dist={:.0f} at=({:.0f},{:.0f},{:.0f})", a->GetFormID(), a->GetName(),
							a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth), a->IsDead() ? 1 : 0, ap.GetDistance(player->GetPosition()), ap.x, ap.y, ap.z);
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
		} else if (cmd == "mirror") {
			// -1: by the game (furniture, killmoves); 0 and 1 force it off and on, for the test
			Pose::ForceMirror(std::stoi(arg));
			logger::info("debug: mirror {}", arg);
		} else if (cmd == "bones") {
			// the player's skeleton's bone names, for the pose mapping (depth-first, indented)
			std::function<void(RE::NiAVObject*, int)> walk = [&](RE::NiAVObject* o, int d) {
				if (!o || d > 9) return;
				logger::info("BONE {}{}", std::string(d * 2, ' '), o->name.c_str());
				if (auto n = o->AsNode())
					for (auto& c : n->GetChildren()) walk(c.get(), d + 1);
			};
			if (player) walk(player->Get3D(false), 0);
		} else if (cmd == "flash") {
			Hud::DumpFlash();
		} else if (cmd == "star") {
			Star::SpawnNow();
			logger::info("debug: star -> next tick");
		} else if (cmd == "snap") {
			Hud::RequestSnapshot();
			logger::info("debug: snap -> next frame");
		} else if (cmd == "react") {
			Reactions::ForceNext();
			logger::info("debug: react -> next tick");
		} else if (!cmd.empty()) {
			logger::info("debug: unknown command '{}'", cmd);
		}
	}
}
