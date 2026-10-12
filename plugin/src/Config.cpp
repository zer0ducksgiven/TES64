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
	telemetry = ini.GetBoolValue("Debug", "bTelemetry", false);
	renderTest = static_cast<int>(ini.GetLongValue("Debug", "iRenderTest", 0));
	if (scale <= 0.01f) scale = 0.48f;
}
