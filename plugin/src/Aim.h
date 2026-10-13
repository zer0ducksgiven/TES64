#pragma once

// Mario's shots go where the crosshair is. The camera sits above and behind him, so Skyrim's own aim (parallel to the
// camera, from his hand) lands well under the crosshair at close range. Every spell and arrow the player launches is
// turned, as it appears, toward the point the crosshair ray hits.
namespace Aim
{
	void Tick(RE::PlayerCharacter* a_player);   // every frame while Mario is out
	void Reset();
}
