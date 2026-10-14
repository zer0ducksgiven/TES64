#pragma once

#include "Sm64.h"

// Gold from the fallen, SM64-style: when someone dies while Mario is out, the gold they carried comes out as coins
// floating round the body (yellow, or blue for the bigger amounts), spinning. Mario touching one takes its gold, as
// picking gold up would (so it heals, Hud.cpp). Coins left when Mario mode ends, or when he dies, go back into the body.
namespace Coins
{
	void Register();   // the death events
	void Reset();      // Mario mode off or his death: uncollected gold back where it came from

	// every SM64 tick: new deaths, pickups. a_toSm64: Skyrim position -> Mario's SM64 space
	void Tick(RE::PlayerCharacter* a_player, const SM64MarioState& a_state, const std::function<std::array<float, 3>(const RE::NiPoint3&)>& a_toSm64);
	// every frame: the coins for Objects to draw, facing a_camera (SM64 units)
	void Put(const float a_camera[3]);
}
