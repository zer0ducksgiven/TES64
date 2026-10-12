#include "Fonts.h"
#include "Config.h"

#include <fstream>
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
		if (!std::filesystem::exists(swf, ec)) {
			logger::warn("fonts: Data/Interface/fonts_tes64.swf is missing (reinstall TES64)");
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
