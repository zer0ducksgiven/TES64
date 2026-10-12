#pragma once

// SM64's objects on screen (the Power Star, coins): their models and texture atlas read from the player's ROM (NG64's
// object loader, tes64_obj.c), all drawn through one template mesh, meshes/TES64/objects.nif, whose vertices are
// filled every frame. Positions are SM64 units, in the same space as Mario's.
namespace Objects
{
	bool Init();   // after Sm64::Init: the models, and objects.dds
	bool Ready();

	int StarMesh();
	int CoinMesh(bool a_blue, int a_frame);   // a coin's spin is four pictures (frame 0-3)

	// one frame's objects: Begin, Put each, End (uploads and shows them under Mario's node)
	void Begin();
	// a_yaw: turned about the vertical (SM64 radians); coins face the camera that way
	void Put(int a_mesh, const float a_pos[3], float a_yaw, float a_scale);
	void End(RE::NiNode* a_marioRoot, const float a_marioSm64[3], float a_scale);
	void Hide();
}
