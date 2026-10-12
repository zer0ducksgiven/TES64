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
	bool                 _haveLastSet = false;
	bool                 _respawnPending = false;
	SM64MarioState       _state{};
	SM64MarioGeometryBuffers _geo{};
	std::vector<float>   _geoPos, _geoNormal, _geoColor, _geoUv;
};
