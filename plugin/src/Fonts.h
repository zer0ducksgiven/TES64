#pragma once

// Skyrim's text in the SM64 font ([HUD] bSM64Fonts): Data/Interface/fontconfig.txt, read by the game's UI at startup,
// is written (or taken away again) before the UI loads. The font is fonts_tes64.swf, built from Mario64.ttf by
// tools/make_font_swf.py. Another mod's fontconfig.txt is left alone.
namespace Fonts
{
	void Apply();
}
