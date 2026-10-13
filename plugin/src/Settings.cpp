#include "Settings.h"
#include "Config.h"

#include <SimpleIni.h>

namespace
{
	enum class Kind { kHeader = 0, kToggle = 1, kSlider = 2, kKey = 3 };

	struct Entry
	{
		Kind        kind;
		const char* label;
		const char* help;
		const char* section;   // TES64.ini section and key (the same names as always)
		const char* key;
		float       min, max, step;
		const char* format;
		float       def;
		float*      f = nullptr;
		bool*       b = nullptr;
		int*        i = nullptr;
		std::uint32_t* u = nullptr;
	};

	// the page, top to bottom (Papyrus: TES64_MCM lays this table out)
	std::vector<Entry> s_table;

	void Build()
	{
		if (!s_table.empty()) return;
		using namespace Config;
		auto header = [](const char* a_label) { Entry e{}; e.kind = Kind::kHeader; e.label = a_label; return e; };
		auto toggle = [](const char* a_label, const char* a_help, const char* a_sec, const char* a_key, bool* a_v, bool a_def) {
			Entry e{}; e.kind = Kind::kToggle; e.label = a_label; e.help = a_help; e.section = a_sec; e.key = a_key; e.b = a_v; e.def = a_def ? 1.0f : 0.0f; return e;
		};
		auto slider = [](const char* a_label, const char* a_help, const char* a_sec, const char* a_key, float* a_v, float a_min, float a_max, float a_step, const char* a_fmt, float a_def) {
			Entry e{}; e.kind = Kind::kSlider; e.label = a_label; e.help = a_help; e.section = a_sec; e.key = a_key; e.f = a_v; e.min = a_min; e.max = a_max; e.step = a_step; e.format = a_fmt; e.def = a_def; return e;
		};
		auto keymap = [](const char* a_label, const char* a_help, const char* a_sec, const char* a_key, std::uint32_t* a_v, std::uint32_t a_def) {
			Entry e{}; e.kind = Kind::kKey; e.label = a_label; e.help = a_help; e.section = a_sec; e.key = a_key; e.u = a_v; e.def = static_cast<float>(a_def); return e;
		};
		s_table = {
			header("Mario mode"),
			keymap("Toggle Mario mode", "Switch between Mario and your Skyrim character.", "Controls", "iToggleKey", &toggleKey, 0x44),
			keymap("Punch / kick / dive (B)", "Mario's own attack button. Jump and crouch use Skyrim's Jump and Sneak. On an Xbox pad: A jump, Y punch, tap X dive, LB crouch.", "Controls", "iAttackKey", &attackKey, 0x2F),
			header("Mario"),
			slider("Size", "Skyrim units per SM64 unit. 0.48 makes Mario about 0.6x an NPC's height. Applies next time Mario mode turns on.", "Mario", "fScale", &scale, 0.3f, 1.0f, 0.02f, "{2}", 0.48f),
			slider("Attack damage", "Mario's punches, kicks and ground pounds on Skyrim's people and creatures.", "Mario", "fDamageMult", &damageMult, 0.25f, 5.0f, 0.25f, "{2}x", 1.0f),
			slider("Weapon size in Mario's hands", "1.0 is the weapon's real Skyrim size, so Mario lugs a sword nearly as big as he is.", "Mario", "fWeaponScale", &weaponScale, 0.4f, 2.0f, 0.05f, "{2}", 1.0f),
			toggle("Skyrim's body movement in attacks", "Mario's torso and head follow Skyrim's swing, cast or block, not just his arms.", "Mario", "bBodyPose", &bodyPose, true),
			slider("Camera lift in combat", "Raises the crosshair above Mario while your weapons are drawn, so you aim at enemies instead of the ground. Applies next time Mario mode turns on.", "Mario", "fCombatCameraLift", &combatCameraLift, 0.0f, 150.0f, 5.0f, "{0}", 60.0f),
			toggle("Bowser wipe when Mario dies", "The screen closes on Bowser's face with his laugh, as in Super Mario 64.", "Mario", "bDeathWipe", &deathWipe, true),
			header("HUD"),
			slider("HUD size", "Mario's meters, counters and compass.", "HUD", "fScale", &hudScale, 0.5f, 2.0f, 0.05f, "{2}x", 1.0f),
			toggle("Hide the crosshair with weapons away", "", "HUD", "bHideCrosshair", &hideCrosshairSheathed, true),
			toggle("SM64-style compass", "Replaces Skyrim's compass while you're Mario.", "HUD", "bCompass", &compass, true),
			toggle("SM64 font for Skyrim's text", "Menus, dialogue, books and messages in the SM64 font. Takes effect after restarting the game.", "HUD", "bSM64Fonts", &sm64Fonts, true),
			header("Sound and people"),
			toggle("Mario's voice and sounds", "From your Super Mario 64 ROM. Takes effect after restarting the game.", "Mario", "bSounds", &sounds, true),
			toggle("SM64 music", "Super Mario 64 music that follows what you're doing: the boss theme in fights, Bowser's theme against dragons and bosses. Needs Mario's sounds on.", "Mario", "bMusic", &music, true),
			slider("Volume", "", "Mario", "iVolume", nullptr, 0.0f, 100.0f, 5.0f, "{0}%", 80.0f),
			toggle("People comment on the plumber", "", "Mario", "bNPCReactions", &reactions, true),
		};
		// volume is the one int-valued slider
		for (auto& e : s_table)
			if (e.kind == Kind::kSlider && !e.f) e.i = &Config::volume;
	}

	const Entry* At(int a_i)
	{
		Build();
		return a_i >= 0 && a_i < static_cast<int>(s_table.size()) ? &s_table[a_i] : nullptr;
	}
	Entry* AtRW(int a_i) { return const_cast<Entry*>(At(a_i)); }

	float Get(const Entry& e)
	{
		if (e.f) return *e.f;
		if (e.i) return static_cast<float>(*e.i);
		if (e.b) return *e.b ? 1.0f : 0.0f;
		if (e.u) return static_cast<float>(*e.u);
		return 0.0f;
	}

	// kept in the file the plugin already reads after the Journal closes (what MCM Helper used to write)
	void Save(const Entry& e)
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(L"Data/MCM/Settings/TES64.ini");
		const float v = Get(e);
		char        text[32];
		if (e.kind == Kind::kToggle || e.kind == Kind::kKey || e.i) std::snprintf(text, sizeof(text), "%d", static_cast<int>(std::lround(v)));
		else std::snprintf(text, sizeof(text), "%.3f", v);
		ini.SetValue(e.section, e.key, text);
		std::error_code ec;
		std::filesystem::create_directories("Data/MCM/Settings", ec);
		ini.SaveFile(L"Data/MCM/Settings/TES64.ini");
	}

	void Set(Entry& e, float v)
	{
		if (e.f) *e.f = std::clamp(v, e.min, e.max);
		else if (e.i) *e.i = std::clamp(static_cast<int>(std::lround(v)), static_cast<int>(e.min), static_cast<int>(e.max));
		else if (e.b) *e.b = v != 0.0f;
		else if (e.u) *e.u = static_cast<std::uint32_t>(std::max(0.0f, v));
		Save(e);
	}

	// ---- the Papyrus natives (TES64_Native.psc, Papyrus/make_pex.py) --------------------------------------------
	std::int32_t Count(RE::StaticFunctionTag*) { Build(); return static_cast<std::int32_t>(s_table.size()); }
	std::int32_t Kind_(RE::StaticFunctionTag*, std::int32_t i) { auto e = At(i); return e ? static_cast<std::int32_t>(e->kind) : 0; }
	std::string  Label(RE::StaticFunctionTag*, std::int32_t i) { auto e = At(i); return e && e->label ? e->label : ""; }
	std::string  Help(RE::StaticFunctionTag*, std::int32_t i) { auto e = At(i); return e && e->help ? e->help : ""; }
	std::string  Format(RE::StaticFunctionTag*, std::int32_t i) { auto e = At(i); return e && e->format ? e->format : "{0}"; }
	bool         GetBool(RE::StaticFunctionTag*, std::int32_t i) { auto e = At(i); return e && Get(*e) != 0.0f; }
	float        GetFloat(RE::StaticFunctionTag*, std::int32_t i) { auto e = At(i); return e ? Get(*e) : 0.0f; }
	std::int32_t GetInt(RE::StaticFunctionTag*, std::int32_t i) { auto e = At(i); return e ? static_cast<std::int32_t>(std::lround(Get(*e))) : 0; }
	void         SetBool(RE::StaticFunctionTag*, std::int32_t i, bool v) { if (auto e = AtRW(i)) Set(*e, v ? 1.0f : 0.0f); }
	void         SetFloat(RE::StaticFunctionTag*, std::int32_t i, float v) { if (auto e = AtRW(i)) Set(*e, v); }
	void         SetInt(RE::StaticFunctionTag*, std::int32_t i, std::int32_t v) { if (auto e = AtRW(i)) Set(*e, static_cast<float>(v)); }
	float        Min(RE::StaticFunctionTag*, std::int32_t i) { auto e = At(i); return e ? e->min : 0.0f; }
	float        Max(RE::StaticFunctionTag*, std::int32_t i) { auto e = At(i); return e ? e->max : 1.0f; }
	float        Step(RE::StaticFunctionTag*, std::int32_t i) { auto e = At(i); return e ? e->step : 1.0f; }
	float        Default(RE::StaticFunctionTag*, std::int32_t i) { auto e = At(i); return e ? e->def : 0.0f; }
	void         Reset(RE::StaticFunctionTag*, std::int32_t i) { if (auto e = AtRW(i)) Set(*e, e->def); }

	bool RegisterNatives(RE::BSScript::IVirtualMachine* a_vm)
	{
		constexpr auto cls = "TES64_Native";
		a_vm->RegisterFunction("Count", cls, Count);
		a_vm->RegisterFunction("Kind", cls, Kind_);
		a_vm->RegisterFunction("Label", cls, Label);
		a_vm->RegisterFunction("Help", cls, Help);
		a_vm->RegisterFunction("Format", cls, Format);
		a_vm->RegisterFunction("GetBool", cls, GetBool);
		a_vm->RegisterFunction("GetFloat", cls, GetFloat);
		a_vm->RegisterFunction("GetInt", cls, GetInt);
		a_vm->RegisterFunction("SetBool", cls, SetBool);
		a_vm->RegisterFunction("SetFloat", cls, SetFloat);
		a_vm->RegisterFunction("SetInt", cls, SetInt);
		a_vm->RegisterFunction("Min", cls, Min);
		a_vm->RegisterFunction("Max", cls, Max);
		a_vm->RegisterFunction("Step", cls, Step);
		a_vm->RegisterFunction("Default", cls, Default);
		a_vm->RegisterFunction("Reset", cls, Reset);
		logger::info("mcm: TES64_Native registered ({} entries)", s_table.size());
		return true;
	}
}

void Settings::Register()
{
	Build();
	SKSE::GetPapyrusInterface()->Register(RegisterNatives);
}

void Settings::DebugSet(int a_index, float a_value) { if (auto e = AtRW(a_index)) Set(*e, a_value); }

int Settings::Count() { Build(); return static_cast<int>(s_table.size()); }
