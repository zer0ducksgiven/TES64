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

	inline int (*rom_normalize)(std::uint8_t*, std::size_t);
	inline int (*load_mario_model)(const std::uint8_t*, std::size_t);
	inline const char* (*rom_error)();

	inline std::vector<std::uint8_t> texture;   // Mario's texture atlas, RGBA, from the ROM

	// loads sm64.dll and the player's ROM; false (logged) if either is missing or wrong
	bool Init();
	bool Ready();
}
