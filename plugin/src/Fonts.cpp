#include "Fonts.h"
#include "Config.h"

#include <fstream>
#include <iterator>
#include <sstream>

namespace
{
	constexpr const char* kMarker = "; TES64: Skyrim's text in the SM64 font";

	// Skyrim's own mappings, with its everyday text fonts pointed at Mario64; the console, the symbols and the
	// scripts (dragon, Daedric, Dwemer...) stay as they are
	constexpr const char* kConfig = R"(fontlib "Interface\fonts_console.swf"
fontlib "Interface\fonts_en.swf"
fontlib "Interface\fonts_tes64.swf"
map "$ConsoleFont" = "Arial" Normal
map "$StartMenuFont" = "Mario64" Normal
map "$DialogueFont" = "Mario64" Normal
map "$EverywhereFont" = "Mario64" Normal
map "$EverywhereBoldFont" = "Mario64" Normal
map "$EverywhereMediumFont" = "Mario64" Normal
map "$DragonFont" = "Dragon_script" Normal
map "$SkyrimBooks" = "Mario64" Normal
map "$HandwrittenFont" = "Mario64" Normal
map "$HandwrittenBold" = "Mario64" Normal
map "$FalmerFont" = "Falmer" Normal
map "$DwemerFont" = "Dwemer" Normal
map "$DaedricFont" = "Daedric" Normal
map "$MageScriptFont" = "Mage Script" Normal
map "$SkyrimSymbolsFont" = "SkyrimSymbols" Normal
map "$SkyrimBooks_UnreadableFont" = "SkyrimBooks_Unreadable" Normal
map "$CClub_Font" = "Mario64" Normal
map "$CClub_Font_Bold" = "Mario64" Normal
validNameChars "` 1234567890-=~!@#$%^&*():_+QWERTYUIOP[]ASDFGHJKL;'ZXCVBNM,./qwertyuiop{}\asdfghjkl;'zxcvbnm<>?|"
)";
}

namespace
{
	// SM64's dialogue font as a Skyrim font file, made from the player's own ROM by sm64.dll (the same code the installer's
	// tes64_fontgen.exe runs). No font file ships with TES64; this is the fallback for a manual install, or if the
	// installer's step was skipped.
	bool MakeFontFromRom(const std::filesystem::path& a_swf)
	{
		if (Config::romPath.empty()) return false;
		std::ifstream f(std::filesystem::path(Config::romPath), std::ios::binary);
		if (!f) return false;
		std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(f)), {});
		HMODULE                   dll = ::LoadLibraryW(L"Data/SKSE/Plugins/TES64/sm64.dll");
		if (!dll || rom.size() != 8 * 1024 * 1024) return false;
		auto normalize = reinterpret_cast<int (*)(std::uint8_t*, std::size_t)>(::GetProcAddress(dll, "tes64_rom_normalize"));
		auto generate = reinterpret_cast<int (*)(const std::uint8_t*, std::size_t, const char*)>(::GetProcAddress(dll, "tes64_font_generate"));
		auto error = reinterpret_cast<const char* (*)()>(::GetProcAddress(dll, "tes64_font_error"));
		if (!normalize || !generate || !normalize(rom.data(), rom.size())) return false;
		std::error_code ec;
		std::filesystem::create_directories(a_swf.parent_path(), ec);
		if (!generate(rom.data(), rom.size(), a_swf.string().c_str())) {
			logger::warn("fonts: couldn't make the SM64 font from the ROM: {}", error ? error() : "?");
			return false;
		}
		logger::info("fonts: SM64's font made from your ROM");
		return true;
	}
}

void Fonts::Apply()
{
	const std::filesystem::path file{ "Data/Interface/fontconfig.txt" }, swf{ "Data/Interface/fonts_tes64.swf" };
	std::error_code ec;
	bool ours = false, exists = std::filesystem::exists(file, ec);
	if (exists) {
		std::ifstream f(file);
		std::string   first;
		std::getline(f, first);
		ours = first.rfind(kMarker, 0) == 0;
	}
	if (Config::sm64Fonts) {
		if (exists && !ours) {
			logger::warn("fonts: Data/Interface/fontconfig.txt belongs to another mod, so Skyrim keeps its fonts");
			return;
		}
		if (!std::filesystem::exists(swf, ec) && !MakeFontFromRom(swf)) {
			logger::warn("fonts: no SM64 font (Data/Interface/fonts_tes64.swf) and it couldn't be made from the ROM, so Skyrim keeps its fonts");
			return;
		}
		std::ofstream out(file, std::ios::trunc);
		out << kMarker << "\n" << kConfig;
		logger::info("fonts: Skyrim's text in the SM64 font");
	} else if (ours) {
		std::filesystem::remove(file, ec);
		logger::info("fonts: Skyrim's own fonts again");
	}
}
