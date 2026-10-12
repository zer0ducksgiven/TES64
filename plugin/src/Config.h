#pragma once

// Data/SKSE/Plugins/TES64.ini; the installer writes sRomPath
namespace Config
{
	inline std::string   romPath;
	inline std::uint32_t toggleKey = 0x44;       // F10 (DirectInput scan code)
	inline std::uint32_t attackKey = 0x2F;       // V: Mario's punch/kick/dive (B)
	inline std::uint32_t attackPad = 0x8000;     // Xbox Y
	inline float         scale = 0.48f;          // Skyrim units per SM64 unit: Mario ends up ~0.6x an NPC's height

	inline float         damageMult = 1.0f;
	inline float         weaponScale = 1.0f;     // [Mario] fWeaponScale: Skyrim's weapons in Mario's hands (1 = their real size: a giant sword)
	inline float         weaponRot[3] = { 0.0f, 0.0f, 0.0f };   // [Mario] fWeaponRotX/Y/Z, degrees, in the hand      // [Mario] fDamageMult: Mario's attacks on Skyrim actors
	inline bool          sounds = true;          // [Mario] bSounds: Mario's voice and sound effects from the ROM
	inline int           volume = 80;            // [Mario] iVolume 0-100
	inline bool          smoothMotion = true;   // [Mario] bSmoothMotion: blend between SM64 ticks (off: step at 30 Hz)
	inline float         hudScale = 1.0f;        // [HUD] fScale: Mario's HUD size
	inline bool          hideCrosshairSheathed = true;   // [HUD] bHideCrosshair: no crosshair with weapons away
	inline bool          compass = true;         // [HUD] bCompass: the SM64-style compass instead of Skyrim's
	inline bool          sm64Fonts = true;       // [HUD] bSM64Fonts: Skyrim's text in the SM64 font (after a restart)
	inline bool          bodyPose = true;        // [Mario] bBodyPose: Skyrim's torso and head movement on Mario during attacks
	inline bool          reactions = true;       // [Mario] bNPCReactions: people comment on the plumber
	inline bool          telemetry = false;
	inline int           renderTest = 0;         // [Debug] iRenderTest: 1 = draw a test cube instead of Mario      // [Debug] bTelemetry: state lines in TES64.log, for the UAT harness

	void Load();
	void LoadMcm();   // MCM Helper's saved settings (Data/MCM/Settings/TES64.ini), after the Journal closes
}
