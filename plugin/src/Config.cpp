#include "Config.h"

#include <SimpleIni.h>

void Config::Load()
{
	CSimpleIniA ini;
	ini.SetUnicode();
	if (ini.LoadFile(L"Data/SKSE/Plugins/TES64.ini") < 0) {
		logger::warn("TES64.ini not found, using defaults");
		return;
	}
	romPath = ini.GetValue("General", "sRomPath", "");
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
	volume = static_cast<int>(ini.GetLongValue("Mario", "iVolume", volume));
	telemetry = ini.GetBoolValue("Debug", "bTelemetry", false);
	renderTest = static_cast<int>(ini.GetLongValue("Debug", "iRenderTest", 0));
	if (scale <= 0.01f) scale = 0.48f;
}
