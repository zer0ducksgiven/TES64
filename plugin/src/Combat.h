#pragma once

#include "Sm64.h"

// Mario <-> Skyrim combat: his punches, kicks, dives, ground pounds and stomps hit Skyrim actors (real damage from
// the player, knockdown on the strong ones) and shove physics clutter; Skyrim's hits on the player knock Mario back.
// Skyrim's health is the truth: the power meter shows it, and damage Mario takes inside SM64 (falls, burns) comes
// off it.
namespace Combat
{
	void Register();   // hit events
	void Reset();      // Mario mode on: forget the last attack / pending knockbacks

	// after each SM64 tick
	void Tick(RE::PlayerCharacter* a_player, std::int32_t a_mario, const SM64MarioState& a_state,
		const RE::NiPoint3& a_marioPos, const std::function<std::array<float, 3>(const RE::NiPoint3&)>& a_toSm64);
}
