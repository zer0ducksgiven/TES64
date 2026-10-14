#include "DebugChannel.h"
#include "Collision.h"
#include "Config.h"
#include "Hud.h"
#include "Combat.h"
#include "Settings.h"
#include "Mario.h"
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
		} else if (cmd == "settings") {
			// every ini / game setting whose name contains arg (case-insensitive): name = value
			std::string want = arg;
			for (auto& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			auto show = [&](RE::Setting* s) {
				if (!s || !s->GetName()) return;
				std::string n = s->GetName(), l = n;
				for (auto& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				if (l.find(want) == std::string::npos) return;
				switch (s->GetType()) {
				case RE::Setting::Type::kFloat: logger::info("SETTING {} = {}", n, s->GetFloat()); break;
				case RE::Setting::Type::kInteger: logger::info("SETTING {} = {}", n, s->GetInteger()); break;
				case RE::Setting::Type::kBool: logger::info("SETTING {} = {}", n, s->GetBool()); break;
				default: logger::info("SETTING {} (other)", n);
				}
			};
			if (auto ini = RE::INISettingCollection::GetSingleton())
				for (auto s : ini->settings) show(s);
		} else if (cmd == "mappedkey") {
			if (auto map = RE::ControlMap::GetSingleton()) {
				const auto key = map->GetMappedKey(arg, RE::INPUT_DEVICE::kKeyboard);
				const auto pad = map->GetMappedKey(arg, RE::INPUT_DEVICE::kGamepad);
				logger::info("debug: mappedkey {} keyboard={:#x} gamepad={:#x}", arg, key, pad);
			}
		} else if (cmd == "setsprint") {
			// the player starts sprinting, as Skyrim would on LB: the plugin must stop it
			if (player && player->AsActorState()) player->AsActorState()->actorState1.sprinting = 1;
			logger::info("debug: setsprint");
		} else if (cmd == "mcmset") {
			std::string value;
			in >> value;
			Settings::DebugSet(std::stoi(arg), std::stof(value));
			logger::info("debug: mcmset {} {}", arg, value);
		} else if (cmd == "hitplayer") {
			// hitplayer light | power | arrow
			Combat::DebugHit(arg == "power", arg == "arrow");
			logger::info("debug: hitplayer {}", arg);
		} else if (cmd == "equipspell") {
			auto spell = RE::TESForm::LookupByID<RE::SpellItem>(static_cast<RE::FormID>(std::stoul(arg, nullptr, 16)));
			if (spell && player) {
				player->AddSpell(spell);
				RE::ActorEquipManager::GetSingleton()->EquipSpell(player, spell, RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x00013F42));
			}
			logger::info("debug: equipspell {} -> {}", arg, spell ? "ok" : "no such spell");
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
						logger::info("DOOR {:08X} ({:.0f},{:.0f},{:.0f}) open={} name={}", ref->GetFormID(), p.x, p.y, p.z,
							RE::BGSOpenCloseForm::GetOpenState(ref) == RE::BGSOpenCloseForm::OPEN_STATE::kOpen ? 1 : 0, std::string(ref->GetName()));
						if (ref->Get3D()) Collision::LogRefBodies(ref);
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
		} else if (cmd == "dumptris") {
			// the streamed triangles within arg units of the player, Skyrim units, 9 floats each, for offline analysis
			std::vector<Collision::Tri> tris;
			Collision::Gather(player->GetParentCell(), player->GetPosition(), arg.empty() ? 2500.0f : std::stof(arg), tris);
			std::ofstream out("Data/SKSE/Plugins/TES64_tris.bin", std::ios::binary);
			for (auto& t : tris)
				for (auto& v : t.v) {
					const float f[3] = { v.x, v.y, v.z };
					out.write(reinterpret_cast<const char*>(f), sizeof(f));
				}
			logger::info("debug: dumptris {} triangles", tris.size());
		} else if (cmd == "doorlinks") {
			// every teleport door within arg units: where it sits, where it sends the player, and the door at the far end
			if (auto tes = RE::TES::GetSingleton(); tes && player) {
				tes->ForEachReferenceInRange(player, arg.empty() ? 3000.0f : std::stof(arg), [&](RE::TESObjectREFR* ref) {
					auto base = ref->GetBaseObject();
					if (!base || !base->Is(RE::FormType::Door)) return RE::BSContainer::ForEachResult::kContinue;
					auto tele = ref->extraList.GetByType<RE::ExtraTeleport>();
					auto td = tele ? tele->teleportData : nullptr;
					if (!td) return RE::BSContainer::ForEachResult::kContinue;
					auto  farDoor = td->linkedDoor.get();
					auto  p = ref->GetPosition();
					logger::info("DOORLINK {:08X} {} at=({:.0f},{:.0f},{:.0f}) sends to=({:.0f},{:.0f},{:.0f}) far={:08X} farcell={}", ref->GetFormID(), std::string(ref->GetName()), p.x, p.y, p.z,
						td->position.x, td->position.y, td->position.z, farDoor ? farDoor->GetFormID() : 0,
						farDoor && farDoor->GetParentCell() ? std::string(farDoor->GetParentCell()->GetName() ? farDoor->GetParentCell()->GetName() : "?") : std::string("unloaded"));
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
		} else if (cmd == "usedoor") {
			// the player activates the nearest teleport door within arg units (default 500), as pressing E on it would
			RE::TESObjectREFR* best = nullptr;
			float              bestD = arg.empty() ? 500.0f : std::stof(arg);
			if (auto tes = RE::TES::GetSingleton(); tes && player) {
				tes->ForEachReferenceInRange(player, bestD, [&](RE::TESObjectREFR* ref) {
					auto base = ref->GetBaseObject();
					if (base && base->Is(RE::FormType::Door) && ref->extraList.HasType(RE::ExtraDataType::kTeleport)) {
						const float d = ref->GetPosition().GetDistance(player->GetPosition());
						if (d < bestD) { bestD = d; best = ref; }
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			if (best) best->ActivateRef(player, 0, nullptr, 1, false);
			logger::info("debug: usedoor -> {}", best ? fmt::format("{:08X} {} at {:.0f}", best->GetFormID(), std::string(best->GetName()), bestD) : "no teleport door in reach");
		} else if (cmd == "watergrid") {
			Mario::GetSingleton()->WaterGrid(player, arg.empty() ? 1500.0f : std::stof(arg), 60.0f);
		} else if (cmd == "raygrid") {
			std::string step;
			in >> step;
			Mario::GetSingleton()->RayGrid(player, arg.empty() ? 1500.0f : std::stof(arg), step.empty() ? 60.0f : std::stof(step));
		} else if (cmd == "hidestatics") {
			Collision::DebugHideStatics(arg == "1");
			logger::info("debug: hidestatics {}", arg);
		} else if (cmd == "skipdoors") {
			Collision::DebugSkipDoors(std::stoi(arg));
			logger::info("debug: skipdoors {}", arg);
		} else if (cmd == "projlog") {
			// projectiles in flight: where, which way, against the camera's forward and the player's own aim angles
			if (auto tes = RE::TES::GetSingleton(); tes && player) {
				float fwd[3] = { 0, 0, 0 };
				if (auto camera = RE::PlayerCamera::GetSingleton(); camera && camera->cameraRoot) {
					const auto& r = camera->cameraRoot->world.rotate;
					fwd[0] = r.entry[0][1]; fwd[1] = r.entry[1][1]; fwd[2] = r.entry[2][1];
				}
				auto mgr = RE::Projectile::Manager::GetSingleton();
				auto each = [&](const RE::BSTArray<RE::ProjectileHandle>& list) {
					for (auto& h : list) {
						auto sp = h.get();
						auto proj = sp.get();
						if (!proj) continue;
						const auto& v = proj->GetProjectileRuntimeData().linearVelocity;
						const auto  p = proj->GetPosition();
						const float hh = std::hypot(v.x, v.y);
						logger::info("PROJ at=({:.0f},{:.0f},{:.0f}) vel=({:.0f},{:.0f},{:.0f}) pitch={:.3f} | camera fwd pitch={:.3f} | player angleX={:.3f} player=({:.0f},{:.0f},{:.0f})",
							p.x, p.y, p.z, v.x, v.y, v.z, std::atan2(v.z, hh), std::asin(fwd[2]), player->GetAngleX(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
					}
				};
				if (mgr) { each(mgr->unlimited); each(mgr->limited); }
				logger::info("debug: projlog {} {}", mgr ? mgr->unlimited.size() : 0, mgr ? mgr->limited.size() : 0);
			}
		} else if (cmd == "soundcats") {
			if (auto data = RE::TESDataHandler::GetSingleton())
				for (auto cat : data->GetFormArray<RE::BGSSoundCategory>())
					if (cat) logger::info("SNCT {:08X} edid='{}' name='{}' vol={:.2f} parent={:08X}", cat->GetFormID(), cat->GetFormEditorID(), cat->GetName(), cat->GetCategoryVolume(), cat->parentCategory ? cat->parentCategory->GetFormID() : 0u);
		} else if (cmd == "wipe") {
			Hud::DebugWipe(arg == "1");
			logger::info("debug: wipe {}", arg);
		} else if (cmd == "boxdoors") {
			Collision::DebugBoxDoors(arg == "1");
			logger::info("debug: boxdoors {}", arg);
		} else if (cmd == "dropout") {
			Mario::GetSingleton()->DebugDropOut();
		} else if (cmd == "floorscan") {
			Mario::GetSingleton()->FloorScan(player, arg.empty() ? 1500.0f : std::stof(arg));
		} else if (cmd == "findmodel") {
			// references within 8000 units whose model path contains arg (case-insensitive)
			if (auto tes = RE::TES::GetSingleton(); tes && player) {
				std::string want = arg;
				for (auto& ch : want) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
				tes->ForEachReferenceInRange(player, 8000.0f, [&](RE::TESObjectREFR* ref) {
					auto base = ref->GetBaseObject();
					auto model = base ? base->As<RE::TESModel>() : nullptr;
					if (model) {
						std::string path = model->GetModel();
						for (auto& ch : path) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
						if (path.find(want) != std::string::npos) {
							auto p = ref->GetPosition();
							logger::info("MODEL {:08X} {} ({:.0f},{:.0f},{:.0f}) type={} 3d={}", ref->GetFormID(), model->GetModel(), p.x, p.y, p.z, static_cast<int>(base->GetFormType()), ref->Get3D() != nullptr);
							if (ref->Get3D()) Collision::LogRefBodies(ref);
						}
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
		} else if (cmd == "raywalls") {
			Mario::GetSingleton()->RayWalls(player, arg.empty() ? 250.0f : std::stof(arg));
		} else if (cmd == "wallscan") {
			Mario::GetSingleton()->WallScan(player, arg.empty() ? 400.0f : std::stof(arg));
		} else if (cmd == "activebodies") {
			Collision::LogActiveBodies(player, arg.empty() ? 1500.0f : std::stof(arg));
		} else if (cmd == "colbodies") {
			Collision::LogBodies(player, arg.empty() ? 1500.0f : std::stof(arg));
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
