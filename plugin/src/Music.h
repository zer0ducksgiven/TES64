#pragma once

// SM64's music that follows what's going on: the boss theme while something fights you, Bowser's theme against a dragon
// or a much stronger foe, silence otherwise (Skyrim's own music keeps the rest). Sequences come from the player's ROM
// through libsm64's sequence player; the same player carries the star jingles, so those get the stage and the music
// comes back after them. Switched by bMusic (MCM).
namespace Music
{
	void Tick(RE::PlayerCharacter* a_player, float a_delta);   // every frame while Mario is out
	void Hold(float a_seconds);                                // a jingle is playing: leave the sequence player alone
	void Reset();                                              // Mario mode off: the music fades
	int  Wanted();                                             // the sequence the context asks for (0: none), for tests
}
