#include "Sm64.h"
#include "Config.h"
#include "Hud.h"

#include <fstream>

namespace
{
	bool s_ready = false;

	template <class T>
	bool Resolve(HMODULE a_dll, T& a_fn, const char* a_name)
	{
		a_fn = reinterpret_cast<T>(::GetProcAddress(a_dll, a_name));
		if (!a_fn) logger::error("sm64.dll is missing {}", a_name);
		return a_fn != nullptr;
	}
}

bool Sm64::Ready() { return s_ready; }

bool Sm64::Init()
{
	HMODULE dll = ::LoadLibraryW(L"Data/SKSE/Plugins/TES64/sm64.dll");
	if (!dll) {
		logger::error("could not load Data/SKSE/Plugins/TES64/sm64.dll");
		return false;
	}
	bool ok = Resolve(dll, global_init, "sm64_global_init") &
	          Resolve(dll, static_surfaces_load, "sm64_static_surfaces_load") &
	          Resolve(dll, mario_create, "sm64_mario_create") &
	          Resolve(dll, mario_tick, "sm64_mario_tick") &
	          Resolve(dll, mario_delete, "sm64_mario_delete") &
	          Resolve(dll, set_mario_position, "sm64_set_mario_position") &
	          Resolve(dll, set_mario_faceangle, "sm64_set_mario_faceangle") &
	          Resolve(dll, set_mario_health, "sm64_set_mario_health") &
	          Resolve(dll, set_mario_water_level, "sm64_set_mario_water_level") &
	          Resolve(dll, set_mario_velocity, "sm64_set_mario_velocity") &
	          Resolve(dll, set_mario_action, "sm64_set_mario_action") &
	          Resolve(dll, mario_take_damage, "sm64_mario_take_damage") &
	          Resolve(dll, mario_kill, "sm64_mario_kill") &
	          Resolve(dll, mario_interact_cap, "sm64_mario_interact_cap") &
	          Resolve(dll, set_mario_invincibility, "sm64_set_mario_invincibility") &
	          Resolve(dll, rom_normalize, "tes64_rom_normalize") &
	          Resolve(dll, load_mario_model, "tes64_load_mario_model") &
	          Resolve(dll, rom_error, "tes64_rom_error");
	if (!ok) return false;
	play_sound = reinterpret_cast<decltype(play_sound)>(::GetProcAddress(dll, "sm64_ng64_play_sound"));
	find_floor_height = reinterpret_cast<decltype(find_floor_height)>(::GetProcAddress(dll, "sm64_surface_find_floor_height"));
	find_wall_collision = reinterpret_cast<decltype(find_wall_collision)>(::GetProcAddress(dll, "sm64_surface_find_wall_collision"));
	find_ceil = reinterpret_cast<decltype(find_ceil)>(::GetProcAddress(dll, "sm64_surface_find_ceil"));
	set_mario_action_arg = reinterpret_cast<decltype(set_mario_action_arg)>(::GetProcAddress(dll, "sm64_set_mario_action_arg"));
	mario_heal = reinterpret_cast<decltype(mario_heal)>(::GetProcAddress(dll, "sm64_mario_heal"));
	music_play = reinterpret_cast<decltype(music_play)>(::GetProcAddress(dll, "sm64_ng64_music_play"));
	music_stop = reinterpret_cast<decltype(music_stop)>(::GetProcAddress(dll, "sm64_ng64_music_stop"));
	music_status = reinterpret_cast<decltype(music_status)>(::GetProcAddress(dll, "sm64_ng64_music_status"));

	std::ifstream f(std::filesystem::path(Config::romPath), std::ios::binary);
	if (Config::romPath.empty() || !f) {
		logger::error("Super Mario 64 ROM not found at '{}' (set sRomPath in TES64.ini, or rerun the installer)", Config::romPath);
		return false;
	}
	rom.assign(std::istreambuf_iterator<char>(f), {});
	if (!rom_normalize(rom.data(), rom.size()) || rom.size() != 8 * 1024 * 1024) {
		logger::error("'{}' is not an 8 MB Super Mario 64 ROM", Config::romPath);
		return false;
	}

	texture.resize(4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT);
	global_init(rom.data(), texture.data());
	if (!load_mario_model(rom.data(), rom.size())) {
		logger::error("could not read Mario's model from the ROM ({}); it must be the US version", rom_error());
		return false;
	}
	Hud::Init(rom.data(), rom.size());
	auto start = reinterpret_cast<int (*)(const std::uint8_t*)>(::GetProcAddress(dll, "tes64_audio_start"));
	audio_volume = reinterpret_cast<void (*)(int)>(::GetProcAddress(dll, "tes64_audio_volume"));
	lock = reinterpret_cast<void (*)()>(::GetProcAddress(dll, "tes64_lock"));
	unlock = reinterpret_cast<void (*)()>(::GetProcAddress(dll, "tes64_unlock"));
	if (Config::sounds && start && start(rom.data())) {
		audio_volume(0);   // silent until Mario mode
		logger::info("audio: Mario's sounds from the ROM");
	} else {
		audio_volume = nullptr;
		logger::info("audio: off");
	}
	s_ready = true;
	logger::info("libsm64 ready");
	return true;
}
