#pragma once

// Skyrim's collision, as triangles for libsm64: the Havok world's fixed bodies (terrain, architecture, rocks, trees)
// near a point, flattened to world-space triangles in Skyrim units.
namespace Collision
{
	struct Tri
	{
		RE::NiPoint3 v[3];
	};

	struct Stats
	{
		std::size_t bodies = 0;
		std::size_t tris = 0;
		std::size_t doors = 0;
		std::size_t moving = 0;      // animated statics read (keyframed, outside the fixed island)
		std::size_t bigHulls = 0;    // convex shapes dropped for having too many vertices       // door bodies read
		std::size_t unhandled = 0;   // shapes of a type this doesn't read yet (logged by type once)
		float       ms = 0.0f;
	};

	// fixed bodies whose AABB touches the box `center` +- `radius` (Skyrim units); only triangles touching the box
	Stats Gather(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_center, float a_radius, std::vector<Tri>& a_out);

	struct BodyInfo
	{
		int          layer, shape;
		bool         wanted;
		std::size_t  tris;
		RE::NiPoint3 mn, mx;   // Skyrim units
	};
	void Bodies(RE::PlayerCharacter* a_player, float a_radius, std::vector<BodyInfo>& a_out);

	void LogActiveBodies(RE::PlayerCharacter* a_player, float a_radius);   // debug: bodies outside the fixed island

	// debug: every fixed body within a_radius of the player: layer, shape type, box, triangles read from it
	void LogBodies(RE::PlayerCharacter* a_player, float a_radius);

	// Skyrim's ground vs the stream: casts a ray down at each point through the game's own physics; where it hits solid
	// ground a_missing says SM64 lacks, the body is added to what the stream reads. Returns the bodies added.
	int Heal(RE::PlayerCharacter* a_player, const std::vector<RE::NiPoint3>& a_points, const std::function<bool(const RE::NiPoint3&)>& a_missing, float a_above = 300.0f);

	void DebugHideStatics(bool a_hide);   // debug: the stream withholds static collision until the self-heal asks for it

	// a number that changes whenever a door near the player moves (swings, opens, closes)
	std::uint64_t DoorSignature(RE::PlayerCharacter* a_player, float a_radius);

	// debug: each door body's place and size as Havok has it (Skyrim units)
	void LogDoorBodies(RE::PlayerCharacter* a_player, float a_radius);
}
