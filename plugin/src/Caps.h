#pragma once

// SM64's caps from Skyrim's magic: invisibility (spell or potion) = Vanish Cap, the armour spells (Oakflesh ..
// Dragonhide) = Metal Cap, the Whirlwind Sprint shout = Wing Cap. Lasts as long as the effect (Wing Cap: SM64's 60 s).
namespace Caps
{
	enum : std::uint32_t
	{
		kVanish = 0x02,   // MARIO_VANISH_CAP
		kMetal = 0x04,    // MARIO_METAL_CAP
		kWing = 0x08,     // MARIO_WING_CAP
	};

	void Register();
	void Tick(RE::PlayerCharacter* a_player, std::int32_t a_mario);   // main thread, every SM64 tick
}
