#pragma once

// TES64's Mod Configuration Menu (SkyUI): the page is Papyrus (TES64_MCM, from tools/make_pex.py) laid out from this
// table through native functions on TES64_Native. Values apply as they change and are kept in
// Data/MCM/Settings/TES64.ini, which Config reads at startup.
namespace Settings
{
	void Register();   // the Papyrus natives (at SKSE load)
	int  Count();
	void DebugSet(int a_index, float a_value);   // debug channel: what the menu does when you change entry a_index
}
