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
		std::size_t unhandled = 0;   // shapes of a type this doesn't read yet (logged by type once)
		float       ms = 0.0f;
	};

	// fixed bodies whose AABB touches the box `center` +- `radius` (Skyrim units); only triangles touching the box
	Stats Gather(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_center, float a_radius, std::vector<Tri>& a_out);
}
