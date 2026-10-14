#pragma once

#include "Sm64.h"

// Mario mode: one libsm64 Mario that drives the player. Ticked from the player's update at SM64's 30 Hz.
class Mario
{
public:
	static Mario* GetSingleton()
	{
		static Mario singleton;
		return &singleton;
	}
	static void InstallHooks();

	void RequestToggle() { _toggleRequested = true; }
	bool Active() const { return _id >= 0; }
	void Update(RE::PlayerCharacter* a_player, float a_delta);

	// Skyrim <-> SM64 space. SM64 is Y-up; Skyrim is Z-up with Y north. Positions are relative to _origin, so
	// SM64's coordinates stay small however far into the world the player is.
	std::array<float, 3> ToSm64(const RE::NiPoint3& a_pos) const;
	RE::NiPoint3         ToSkyrim(const float a_pos[3]) const;

private:
	void Enable(RE::PlayerCharacter* a_player);
	void Disable();
	void StreamWorld(RE::PlayerCharacter* a_player, bool a_force);
 public:
	void HealCollision(RE::PlayerCharacter* a_player);
	float GroundAtFeet(RE::PlayerCharacter* a_player, bool a_heal);   // SM64 floor at the origin, at his feet (not an arch or roof over him); heals missing collision   // finds ground the stream missed (bridges, animated statics) and adds it
	void DebugDropOut();   // debug: Mario is put far under the world, as if he had fallen out of it
	void SuppressSprint(RE::PlayerCharacter* a_player);   // clears Skyrim's sprint (before and after the player updates)
	void FloorScan(RE::PlayerCharacter* a_player, float a_radius);
	void WallScan(RE::PlayerCharacter* a_player, float a_radius);
	void RayWalls(RE::PlayerCharacter* a_player, float a_range);   // Skyrim's own horizontal rays against SM64's walls, all round the player   // a map of where SM64 walls stand round the player
	void WaterGrid(RE::PlayerCharacter* a_player, float a_radius, float a_step);   // debug: where the cell claims water, and whether the game agrees
	void RayGrid(RE::PlayerCharacter* a_player, float a_radius, float a_step);   // debug: Skyrim's ground vs SM64's floor   // debug: SM64's floor over each thin body vs its top
 private:
	RE::NiPoint3 _respawnLast;
	float        _respawnStill = 0.0f;
	float             _healTimer = 0.0f;
	float             _safe[3] = {};           // the last solid footing (SM64 units): where he goes if he ends up over nothing
	bool              _haveSafe = false;
	int               _noFloorTicks = 0;
	int               _respawnWaits = 0;   // frames waited for the spawn spot's ground to load
	RE::BGSVoiceType* _savedVoice = nullptr;   // the player's own voice while it's silenced (-1: it had none)
	void UpdateWater(RE::PlayerCharacter* a_player);
	static void SetPlayerVisible(RE::PlayerCharacter* a_player, bool a_visible);
	bool Respawn(RE::PlayerCharacter* a_player);
	void Tick(RE::PlayerCharacter* a_player);
	void ResetPrev();   // the last tick before this one = the current one (spawn, respawn): nothing to blend from

	std::atomic<bool>    _toggleRequested{ false };
	std::int32_t         _id = -1;
	RE::NiPoint3         _origin;
	float                _accum = 0.0f;
	float                _telemetryTimer = 0.0f;
	// smoothness stats (telemetry): frames drawn, SM64 ticks, and frames where Mario moved but the player didn't
	float                _statTimer = 0.0f;
	int                  _statFrames = 0, _statTicks = 0, _statStalls = 0;
	double               _statCostMs = 0.0;   // time spent in Mario::Update this second
	RE::NiPoint3         _statPrevPlayer;
	bool                 _statMoving = false;   // SM64 moved Mario on the last tick
	// the tick before the current one, for blending the frames in between (SM64 runs at 30 Hz, the game at 60)
	float                _prevSm64[3]{};
	std::uint32_t        _prevTris = 0;
	std::vector<float>   _prevGeoPos;
	bool                 _seenUpdate = false;
	std::optional<float> _savedGravity;
	RE::NiPoint3         _streamCenter;
	RE::bhkWorld*        _streamWorld = nullptr;
	bool                 _streamed = false;
	RE::NiPoint3         _lastSet;
	const void*          _lastPlace = nullptr;   // the interior cell, or the worldspace outside: a door between them is a move however short
	bool                 _haveLastSet = false;
	bool                 _respawnPending = false;
	SM64MarioState       _state{};
	SM64MarioGeometryBuffers _geo{};
	std::vector<float>   _geoPos, _geoNormal, _geoColor, _geoUv;
};
