#include "Music.h"
#include "Config.h"
#include "Sm64.h"

namespace
{
	constexpr std::uint8_t kSeqBoss = 0x16;        // SEQ_EVENT_BOSS: the mini-boss fights
	constexpr std::uint8_t kSeqBowser = 0x07;      // SEQ_LEVEL_BOSS_KOOPA: Bowser's
	constexpr float        kStartAfter = 1.0f;     // seconds of fighting before the music starts
	constexpr float        kStopAfter = 5.0f;      // seconds of peace before it fades
	constexpr int          kFadeFrames = 90;

	int   s_wanted = 0, s_playing = 0;
	float s_log = 0.0f, s_fight = 0.0f, s_calm = 0.0f, s_hold = 0.0f, s_scan = 0.0f;
	bool  s_boss = false;

	// Skyrim's own music (the "$Music" sound category) is turned down while SM64's plays, and back when it stops
	void Duck(bool a_duck)
	{
		static float s_saved = -1.0f;
		auto         cat = RE::TESForm::LookupByID<RE::BGSSoundCategory>(0x00071E64);
		if (!cat) return;
		if (a_duck && s_saved < 0.0f) {
			s_saved = cat->GetCategoryVolume();
			cat->SetCategoryVolume(s_saved * 0.1f);
		} else if (!a_duck && s_saved >= 0.0f) {
			cat->SetCategoryVolume(s_saved);
			s_saved = -1.0f;
		}
	}

	// who is fighting the player right now, and is any of them a boss
	bool Fighting(RE::PlayerCharacter* a_player, bool& a_boss)
	{
		a_boss = false;
		bool any = false;
		auto lists = RE::ProcessLists::GetSingleton();
		if (!lists) return a_player->IsInCombat();
		const auto  pos = a_player->GetPosition();
		const float myLevel = static_cast<float>(a_player->GetLevel());
		for (auto& handle : lists->highActorHandles) {
			auto actor = handle.get();
			if (!actor || actor.get() == a_player || actor->IsDead() || !actor->IsInCombat()) continue;
			if (!actor->IsHostileToActor(a_player) || actor->GetPosition().GetDistance(pos) > 3500.0f) continue;
			// fighting the player (or their followers): its target is the player
			auto target = actor->GetActorRuntimeData().currentCombatTarget.get();
			if (target.get() != a_player && !actor->IsCommandedActor()) {
				if (!target || !target->IsPlayerTeammate()) continue;
			}
			any = true;
			if (actor->HasKeywordString("ActorTypeDragon") || actor->GetLevel() >= myLevel + 15.0f) a_boss = true;
		}
		return any;
	}
}

void Music::Hold(float a_seconds) { s_hold = std::max(s_hold, a_seconds); }

int Music::Wanted() { return s_wanted; }

void Music::Reset()
{
	Duck(false);
	if (s_playing && Sm64::music_stop) Sm64::music_stop(kFadeFrames);
	s_playing = s_wanted = 0;
	s_fight = s_calm = s_hold = s_scan = 0.0f;
	s_boss = false;
}

void Music::Tick(RE::PlayerCharacter* a_player, float a_delta)
{
	if (!Config::music || !Config::sounds || !Sm64::music_play || !Sm64::music_status) {
		if (s_playing) Reset();
		Duck(false);
		return;
	}
	s_hold = std::max(0.0f, s_hold - a_delta);
	if ((s_scan -= a_delta) <= 0.0f) {
		s_scan = 0.25f;
		bool       boss = false;
		const bool fighting = Fighting(a_player, boss);
		if (fighting) {
			s_fight += 0.25f;
			s_calm = 0.0f;
			if (boss) s_boss = true;
		} else {
			s_calm += 0.25f;
			s_fight = 0.0f;
			if (s_calm >= kStopAfter) s_boss = false;
		}
		if (!s_wanted && s_fight >= kStartAfter) s_wanted = s_boss ? kSeqBowser : kSeqBoss;
		else if (s_wanted && s_calm >= kStopAfter) s_wanted = 0;
		else if (s_wanted == kSeqBoss && s_boss) s_wanted = kSeqBowser;   // a dragon joined in: the music escalates
	}

	if (Config::telemetry && (s_log -= a_delta) <= 0.0f) {
		s_log = 5.0f;
		static auto stats = reinterpret_cast<void (*)(long*, long*, long*)>(::GetProcAddress(::GetModuleHandleW(L"sm64.dll"), "ng64_audio_stats"));
		long under = 0, gap = 0, level = -1;
		if (stats) stats(&under, &gap, &level);
		logger::info("MUSIC state wanted={:#x} playing={:#x} status={:#x} output level={}", s_wanted, s_playing, Sm64::music_status(), level);
	}
	if (s_wanted) {
		if (s_hold > 0.0f) return;
		// the sequence player: what's loaded (low byte), enabled (bit 8)
		const std::uint32_t status = Sm64::music_status();
		const bool          running = (status & 0xFF) == static_cast<std::uint32_t>(s_wanted) && (status & 0x100);
		if (!running) {
			Sm64::music_play(static_cast<std::uint8_t>(s_wanted));
			s_playing = s_wanted;
			Duck(true);
			if (Config::telemetry) logger::info("MUSIC start sequence {:#x} ({})", s_wanted, s_wanted == kSeqBowser ? "bowser" : "boss");
		}
	} else if (s_playing) {
		if (Sm64::music_stop) Sm64::music_stop(kFadeFrames);
		s_playing = 0;
		Duck(false);
		if (Config::telemetry) logger::info("MUSIC fade out");
	}
}
