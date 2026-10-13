#pragma once

// Mario's HUD, drawn over Skyrim's (which stays as it is: health/magicka/stamina, compass, XP): SM64's power meter,
// coin and star counters, with SM64's own graphics from the ROM (hud.c, from NG64). Coins are the player's gold
// (gold picked up heals, as SM64's coins do), stars are completed quests.
namespace Hud
{
	bool Init(const std::uint8_t* a_rom, std::size_t a_len);   // pull the graphics from the ROM
	void InstallHook();
	void DumpFlash();   // debug: the HUD movie's clips in the log
	void DebugWipe(bool a_on);   // debug: the Bowser wipe without dying
	void RequestSnapshot();                                       // telemetry: the next frame is written to TES64_snap.bmp                                          // the swap chain's Present

	// main thread, every frame
	void Tick(RE::PlayerCharacter* a_player, bool a_marioActive, int a_wedges, float a_delta);
}
