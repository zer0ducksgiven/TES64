#pragma once

// Skyrim's arm animations on Mario. While the player attacks, casts, blocks or draws/sheathes a weapon, the hidden
// player's skeleton plays Skyrim's animation; Mario's arm parts (SM64's rigid parts: shoulder, forearm, hand on each
// side) are turned to point where the skeleton's arm segments point, so his swing is Skyrim's swing. His legs, body
// and movement stay SM64's. Eased in and out, so he doesn't snap between his own pose and Skyrim's.
namespace Pose
{
	struct Arm
	{
		float m[3][16];      // the three parts' matrices (row-vector, SM64 axes), relative to Mario's position
		float handRel[3];    // the hand's place, relative to Mario's position, SM64 axes
	};

	void Reset();

	// after each SM64 tick: this tick's part matrices (when Mario has the usual 16 parts)
	void OnTick(const float a_marioPos[3], int a_partCount);

	// each frame, before the ticks: is Skyrim animating an arm action, and how much of it shows
	void Track(RE::PlayerCharacter* a_player, float a_delta);
	// each frame, after the ticks: the arms' matrices, from the skeleton, blended between the last two ticks
	void Compute(RE::PlayerCharacter* a_player, float a_alpha);

	bool        Active();   // an action is on (and a moment after): Mario faces the crosshair
	float       Weight();   // 0 = Mario's own arms, 1 = Skyrim's
	const Arm*  Right();    // nullptr when Skyrim's pose isn't showing
	const Arm*  Left();

	// the arm triangles' places under the pose: rel = offset from Mario (SM64 axes), nrm = normal. False: not posed.
	bool Transform(int a_tri, int a_corner, float a_rel[3], float a_nrm[3]);
}
