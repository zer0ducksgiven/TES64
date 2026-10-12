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
	          Resolve(dll, set_mario_invincibility, "sm64_set_mario_invincibility") &
	          Resolve(dll, rom_normalize, "tes64_rom_normalize") &
	          Resolve(dll, load_mario_model, "tes64_load_mario_model") &
	          Resolve(dll, rom_error, "tes64_rom_error");
	if (!ok) return false;

	std::ifstream f(std::filesystem::path(Config::romPath), std::ios::binary);
	if (Config::romPath.empty() || !f) {
		logger::error("Super Mario 64 ROM not found at '{}' (set sRomPath in TES64.ini, or rerun the installer)", Config::romPath);
		return false;
	}
	std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(f)), {});
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
	s_ready = true;
	logger::info("libsm64 ready");
	return true;
}
