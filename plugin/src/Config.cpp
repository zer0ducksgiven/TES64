#include "Config.h"

#include <SimpleIni.h>

namespace
{
	// the player-facing settings: TES64.ini first, then what MCM Helper saved (its own ini, same keys)
	void ApplySettings(const CSimpleIniA& ini)
	{
		using namespace Config;
		toggleKey = static_cast<std::uint32_t>(ini.GetLongValue("Controls", "iToggleKey", toggleKey));
		attackKey = static_cast<std::uint32_t>(ini.GetLongValue("Controls", "iAttackKey", attackKey));
		attackPad = static_cast<std::uint32_t>(ini.GetLongValue("Controls", "iAttackGamepad", attackPad));
		scale = static_cast<float>(ini.GetDoubleValue("Mario", "fScale", scale));
		damageMult = static_cast<float>(ini.GetDoubleValue("Mario", "fDamageMult", damageMult));
		weaponScale = static_cast<float>(ini.GetDoubleValue("Mario", "fWeaponScale", weaponScale));
		weaponRot[0] = static_cast<float>(ini.GetDoubleValue("Mario", "fWeaponRotX", weaponRot[0]));
		weaponRot[1] = static_cast<float>(ini.GetDoubleValue("Mario", "fWeaponRotY", weaponRot[1]));
		weaponRot[2] = static_cast<float>(ini.GetDoubleValue("Mario", "fWeaponRotZ", weaponRot[2]));
		sounds = ini.GetBoolValue("Mario", "bSounds", sounds);
		volume = std::clamp(static_cast<int>(ini.GetLongValue("Mario", "iVolume", volume)), 0, 100);
		reactions = ini.GetBoolValue("Mario", "bNPCReactions", reactions);
		bodyPose = ini.GetBoolValue("Mario", "bBodyPose", bodyPose);
		hudScale = std::clamp(static_cast<float>(ini.GetDoubleValue("HUD", "fScale", hudScale)), 0.5f, 2.0f);
		hideCrosshairSheathed = ini.GetBoolValue("HUD", "bHideCrosshair", hideCrosshairSheathed);
		compass = ini.GetBoolValue("HUD", "bCompass", compass);
		sm64Fonts = ini.GetBoolValue("HUD", "bSM64Fonts", sm64Fonts);
		smoothMotion = ini.GetBoolValue("Mario", "bSmoothMotion", smoothMotion);
		if (scale <= 0.01f) scale = 0.48f;
	}
}

void Config::Load()
{
	CSimpleIniA ini;
	ini.SetUnicode();
	if (ini.LoadFile(L"Data/SKSE/Plugins/TES64.ini") < 0) {
		logger::warn("TES64.ini not found, using defaults");
	} else {
		romPath = ini.GetValue("General", "sRomPath", "");
		ApplySettings(ini);
		telemetry = ini.GetBoolValue("Debug", "bTelemetry", false);
		renderTest = static_cast<int>(ini.GetLongValue("Debug", "iRenderTest", 0));
	}
	LoadMcm();
}

void Config::LoadMcm()
{
	CSimpleIniA mcm;
	mcm.SetUnicode();
	if (mcm.LoadFile(L"Data/MCM/Settings/TES64.ini") >= 0) {
		ApplySettings(mcm);
		logger::info("settings from the Mod Configuration Menu");
	}
}
