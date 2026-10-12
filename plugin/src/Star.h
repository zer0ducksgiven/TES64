#pragma once

#include "Sm64.h"

// A Power Star for each quest completed while playing as Mario: it springs out of him a few metres away (on open
// ground, not in a wall), spins there until he touches it, and then he does SM64's star dance with the jingle while a
// celebration star circles him. The star's model and texture are SM64's, read from the player's ROM (NG64's object
// loader, tes64_obj.c); it is drawn through meshes/TES64/star.nif, a template whose vertices are filled here.
namespace Star
{
	void Init();    // after Sm64::Init: the model and objects.dds from the ROM
	void Reset();   // Mario mode off / respawned: any star goes

	// every SM64 tick, after mario_tick: quests, the star's flight, its pickup, the dance
	void Tick(RE::PlayerCharacter* a_player, std::int32_t a_mario, const SM64MarioState& a_state);
	// every frame: drawn under Mario's node (a_root: where it sits, Skyrim units), between the last two ticks
	void Draw(RE::NiNode* a_marioRoot, const RE::NiPoint3& a_root, const float a_marioSm64[3], float a_alpha, float a_scale);

	void SpawnNow();   // debug channel: a star as if a quest had just been completed
	bool Dancing();    // Mario is in his star dance (the camera holds still on his front)
}
