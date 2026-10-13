#pragma once

// Skyrim's text in the SM64 font ([HUD] bSM64Fonts): Data/Interface/fontconfig.txt, read by the game's UI at startup,
// is written (or taken away again) before the UI loads. The font is fonts_tes64.swf, made from the player's own ROM
// (SM64's dialogue font, native/sm64/fontgen.c): by the installer, or here at startup if it isn't there yet. Another
// mod's fontconfig.txt is left alone.
namespace Fonts
{
	void Apply();
}
