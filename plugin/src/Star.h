#pragma once

#include "Sm64.h"

// A Power Star for each quest completed while playing as Mario: it springs out of him a few metres away (on open
// ground, not in a wall), spins there until he touches it, and then he does SM64's star dance with the jingle while a
// celebration star circles him. The star is SM64's, from the player's ROM; Objects draws it.
namespace Star
{
	bool IsPlayerQuest(RE::TESQuest* a_quest);   // a quest the player sees in the journal (the only kind that earns a star)
	void Init();    // after Objects::Init
	void Reset();   // Mario mode off / respawned: any star goes

	// every SM64 tick, after mario_tick: quests, the star's flight, its pickup, the dance
	void Tick(RE::PlayerCharacter* a_player, std::int32_t a_mario, const SM64MarioState& a_state);
	// every frame: the star for Objects to draw, between the last two ticks
	void Put(float a_alpha);

	void SpawnNow();   // debug channel: a star as if a quest had just been completed
	bool Dancing();    // Mario is in his star dance (the camera holds still on his front)
}
