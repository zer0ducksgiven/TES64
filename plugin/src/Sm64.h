#pragma once

// sm64.dll is built with MinGW (libsm64 needs GCC), so it's loaded at runtime and called through its C exports
#define SM64_LIB_FN
#include "libsm64.h"
#undef SM64_LIB_FN

namespace Sm64
{
	inline decltype(&sm64_global_init)          global_init;
	inline decltype(&sm64_static_surfaces_load) static_surfaces_load;
	inline decltype(&sm64_mario_create)         mario_create;
	inline decltype(&sm64_mario_tick)           mario_tick;
	inline decltype(&sm64_mario_delete)         mario_delete;
	inline decltype(&sm64_set_mario_position)   set_mario_position;
	inline decltype(&sm64_set_mario_faceangle)  set_mario_faceangle;
	inline decltype(&sm64_set_mario_health)     set_mario_health;
	inline decltype(&sm64_set_mario_water_level) set_mario_water_level;
	inline decltype(&sm64_set_mario_velocity)   set_mario_velocity;
	inline decltype(&sm64_set_mario_action)     set_mario_action;
	inline decltype(&sm64_mario_take_damage)    mario_take_damage;
	inline decltype(&sm64_mario_kill)           mario_kill;
	inline decltype(&sm64_mario_interact_cap)   mario_interact_cap;
	inline decltype(&sm64_set_mario_invincibility) set_mario_invincibility;

	inline int (*rom_normalize)(std::uint8_t*, std::size_t);
	inline int (*load_mario_model)(const std::uint8_t*, std::size_t);
	inline const char* (*rom_error)();

	inline std::vector<std::uint8_t> rom;       // kept: libsm64's audio reads its sound banks from it
	inline std::vector<std::uint8_t> texture;
	inline void (*audio_volume)(int) = nullptr;
	inline void (*play_sound)(std::uint32_t) = nullptr;   // sm64_ng64_play_sound: a sound from the ROM at Mario
	inline void (*lock)() = nullptr;
	inline void (*unlock)() = nullptr;   // Mario's texture atlas, RGBA, from the ROM

	// loads sm64.dll and the player's ROM; false (logged) if either is missing or wrong
	bool Init();
	bool Ready();
}
