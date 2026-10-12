#pragma once

#include "Sm64.h"

// Skyrim's arms on Mario: the player's drawn weapon/shield cloned into Mario's hands (libsm64's body-part matrices,
// NG64's parts patch), spell hand nodes moved onto his hands, and his facing/pose following Skyrim's attacks and casts
// (Skyrim does the hit detection and damage; the hidden player swings and casts as usual).
namespace Arms
{
	struct Hand
	{
		RE::NiPoint3  pos;     // Skyrim space
		RE::NiMatrix3 rot;     // hand's own axes, Skyrim space
		RE::NiMatrix3 grip;    // a held weapon's axes: blade (+Y) across the fist, edge (+X) along the forearm
		bool          valid = false;
	};

	// from libsm64's part matrices this tick
	void ReadHands(const std::function<RE::NiPoint3(const float*)>& a_toSkyrim, float a_heading);
	const Hand& RightHand();
	const Hand& LeftHand();

	// every frame, after Render::Update: the weapon/shield clones follow the hands (under Mario's node at a_root)
	// a_alpha blends the hands between the last two ticks, as Mario's body is (Render.cpp)
	void Update(RE::PlayerCharacter* a_player, RE::NiNode* a_marioRoot, const RE::NiPoint3& a_root, float a_alpha);
	void Clear();

	// Skyrim started an attack/cast this tick: true once per attack
	bool SkyrimActionStarted(RE::PlayerCharacter* a_player);
}
