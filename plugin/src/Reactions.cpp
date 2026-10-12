#include "Reactions.h"
#include "Config.h"

#include <random>

namespace
{
	constexpr std::array kGuard = {
		"Halt! ...Wait. You're the plumber they warned us about.",
		"Let me guess. Someone stole your sweetroll. And your princess.",
		"No jumping on the city walls. I mean it.",
		"I used to be an adventurer like you. Then I took a ground pound to the knee.",
		"Keep those mushrooms out of this hold, red.",
		"Never seen a Breton that short. Or that red.",
	};
	constexpr std::array kCitizen = {
		"Mara's mercy, what is that little man?",
		"Is he... bouncing?",
		"By the Nine, he just jumped clean over my cart!",
		"Fine hat. Very red.",
		"Did that man just say 'yahoo'?",
		"Overalls in Skyrim? He'll freeze.",
		"Talos guide me, there's a plumber in the village.",
		"Mind the chickens, little one.",
	};

	float        s_timer = 30.0f;
	bool         s_force = false;
	std::mt19937 s_rng{ std::random_device{}() };

	void Notify(const std::string& a_text)
	{
		using func_t = void(const char*, const char*, bool);
		static REL::Relocation<func_t> func{ REL::RelocationID(52050, 52933) };
		func(a_text.c_str(), nullptr, true);
	}

	RE::Actor* Witness(RE::PlayerCharacter* a_player)
	{
		RE::Actor* best = nullptr;
		float      bestDist = 1500.0f;
		auto       processes = RE::ProcessLists::GetSingleton();
		if (!processes) return nullptr;
		int seen = 0, people = 0, inRange = 0;
		for (auto& handle : processes->highActorHandles) {
			auto actor = handle.get().get();
			seen++;
			if (!actor || actor->IsDead() || actor->IsHostileToActor(a_player) || actor->IsInCombat() || !actor->GetActorBase()) continue;
			if (!actor->GetRace() || !actor->GetRace()->HasKeywordString("ActorTypeNPC")) continue;   // people, not chickens
			people++;
			const float d = actor->GetPosition().GetDistance(a_player->GetPosition());
			if (d < bestDist) inRange++;
			bool los = false;
			if (d < bestDist && actor->HasLineOfSight(a_player, los) && los) {
				best = actor;
				bestDist = d;
			}
		}
		if (s_force && Config::telemetry) logger::info("REACT candidates: {} high, {} people, {} in range", seen, people, inRange);
		return best;
	}
}

void Reactions::ForceNext() { s_force = true; }

void Reactions::Tick(RE::PlayerCharacter* a_player, float a_delta)
{
	if (!Config::reactions && !s_force) return;
	if (!s_force && (s_timer -= a_delta) > 0.0f) return;
	s_timer = std::uniform_real_distribution<float>(45.0f, 90.0f)(s_rng);

	auto who = Witness(a_player);
	if (!who) {
		if (s_force) logger::info("REACT nobody in sight");
		s_force = false;
		return;
	}
	s_force = false;
	const bool  guard = who->IsGuard();
	const char* line = guard ? kGuard[s_rng() % kGuard.size()] : kCitizen[s_rng() % kCitizen.size()];
	const std::string text = std::string(who->GetDisplayFullName()) + ": " + line;
	Notify(text);
	if (Config::telemetry) logger::info("REACT {}", text);
}
