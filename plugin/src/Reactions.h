#pragma once

// Skyrim's people noticing the plumber: every so often a nearby, non-hostile NPC who can see Mario says something
// (as a notification: "Lucan Valerius: Is he... bouncing?"). Guards have their own lines.
namespace Reactions
{
	void Tick(RE::PlayerCharacter* a_player, float a_delta);
	void ForceNext();   // debug channel: comment now
}
