#include "Mario.h"
#include "Config.h"
#include "Input.h"
#include "Collision.h"
#include "Render.h"
#include "Camera.h"
#include "Combat.h"
#include "Arms.h"
#include "Pose.h"
#include "Coins.h"
#include "Objects.h"
#include "Star.h"
#include "DebugChannel.h"
#include "Hud.h"
#include "Caps.h"
#include "Reactions.h"

namespace
{
	constexpr float kTick = 1.0f / 30.0f;
	constexpr auto  kTakenControls = static_cast<RE::ControlMap::UEFlag>(
		static_cast<std::uint32_t>(RE::ControlMap::UEFlag::kJumping) | static_cast<std::uint32_t>(RE::ControlMap::UEFlag::kSneaking));

	void Notify(const char* a_text)
	{
		using func_t = void(const char*, const char*, bool);
		static REL::Relocation<func_t> func{ REL::RelocationID(52050, 52933) };
		func(a_text, nullptr, true);
	}

	struct PlayerUpdateHook
	{
		static void thunk(RE::PlayerCharacter* a_player, float a_delta)
		{
			func(a_player, a_delta);
			Mario::GetSingleton()->Update(a_player, a_delta);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void Mario::InstallHooks()
{
	REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
	PlayerUpdateHook::func = vtbl.write_vfunc(0xAD, PlayerUpdateHook::thunk);
}

std::array<float, 3> Mario::ToSm64(const RE::NiPoint3& a_pos) const
{
	float s = Config::scale;
	return { (a_pos.x - _origin.x) / s, (a_pos.z - _origin.z) / s, -(a_pos.y - _origin.y) / s };
}

RE::NiPoint3 Mario::ToSkyrim(const float a_pos[3]) const
{
	float s = Config::scale;
	return { _origin.x + a_pos[0] * s, _origin.y - a_pos[2] * s, _origin.z + a_pos[1] * s };
}

// the camera's heading (0 = north, clockwise), for telemetry: the harness steers relative to it
static float CameraYaw()
{
	auto camera = RE::PlayerCamera::GetSingleton();
	if (!camera || !camera->cameraRoot) return 0.0f;
	const auto& r = camera->cameraRoot->world.rotate;
	return std::atan2(r.entry[0][1], r.entry[1][1]);
}

namespace
{
	// adds the time a scope takes to a counter (telemetry only, but cheap enough to always run)
	struct ScopeCost
	{
		double&                                        total;
		std::chrono::steady_clock::time_point          start = std::chrono::steady_clock::now();
		~ScopeCost() { total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); }
	};
}

void Mario::Update(RE::PlayerCharacter* a_player, float a_delta)
{
	ScopeCost cost{ _statCostMs };
	if (Config::telemetry && !_seenUpdate) {
		_seenUpdate = true;
		logger::info("player update running");
	}
	DebugChannel::Poll();
	if (_toggleRequested.exchange(false)) {
		if (Active()) Disable();
		else Enable(a_player);
	}
	Hud::Tick(a_player, Active(), static_cast<int>(_state.health >> 8), a_delta);
	if (Sm64::audio_volume) {
		auto ui = RE::UI::GetSingleton();
		const int want = Active() && !(ui && ui->GameIsPaused()) ? Config::volume : 0;
		static int s_volume = -1;
		if (want != s_volume) Sm64::audio_volume(s_volume = want);
	}
	if (!Active()) return;

	auto ui = RE::UI::GetSingleton();
	if (ui && ui->GameIsPaused()) return;   // menus stop Mario with the game
	Reactions::Tick(a_player, a_delta);

	// Skyrim moved the player (a door, fast travel, coc, a script): Mario follows instead of dragging them back
	// (a door can put the player down less than 300 units away, across a worldspace or cell change: that's a move too)
	const void* place = [&]() -> const void* {
		auto cell = a_player->GetParentCell();
		if (cell && cell->IsInteriorCell()) return cell;
		return a_player->GetWorldspace();
	}();
	if (_haveLastSet && (a_player->GetPosition().GetDistance(_lastSet) > 300.0f || place != _lastPlace)) _respawnPending = true;
	if (_respawnPending) {
		// a door or fast travel puts the player down in steps (the marker, then where the game settles them): wait until
		// they've stopped moving for a moment, so Mario starts where Skyrim's character ends up, not at the door
		const auto now = a_player->GetPosition();
		if (now.GetDistance(_respawnLast) > 2.0f) _respawnStill = 0.0f;
		else _respawnStill += a_delta;
		_respawnLast = now;
		if (_respawnStill < 0.3f) return;
		if (!Respawn(a_player)) return;   // the new cell's collision isn't loaded yet: try next frame
		_respawnStill = 0.0f;
	}

	Pose::Track(a_player, a_delta);
	if ((_healTimer -= a_delta) <= 0.0f) {
		_healTimer = 0.25f;
		HealCollision(a_player);
	}
	_accum += a_delta;
	if (_accum > 0.25f) _accum = kTick;   // after a hitch, don't fast-forward
	const RE::NiPoint3 sm64Before{ _state.position[0], _state.position[1], _state.position[2] };
	bool               ticked = false;
	while (_accum >= kTick) {
		_accum -= kTick;
		_statTicks++;
		ticked = true;
		StreamWorld(a_player, false);
		UpdateWater(a_player);
		Tick(a_player);
	}

	// the player follows Mario's position.
	// Skyrim's own physics stays out of it: no gravity pulling the player under Mario mid-jump, no fall damage
	// on top of Mario's own
	if (auto controller = a_player->GetCharController()) {
		if (!_savedGravity) _savedGravity = controller->gravity;
		controller->gravity = 0.0f;
		controller->fallTime = 0.0f;
		controller->SetLinearVelocityImpl(RE::hkVector4(0.0f, 0.0f, 0.0f, 0.0f));
	}
	// drawn between the last two ticks: the frame's own time along SM64's 30 Hz, so motion is smooth at the game's rate
	// without adding latency (the frame is at most one tick behind the simulation)
	const float alpha = Config::smoothMotion ? std::clamp(_accum / kTick, 0.0f, 1.0f) : 1.0f;
	float       smPos[3];
	for (int axis = 0; axis < 3; axis++) smPos[axis] = _prevSm64[axis] + (_state.position[axis] - _prevSm64[axis]) * alpha;
	auto pos = ToSkyrim(smPos);
	if (Pose::Mirroring()) {
		// furniture and killmoves: Skyrim moves the player, and Mario goes with them (not the other way)
		pos = a_player->GetPosition();
		const auto sm = ToSm64(pos);
		std::copy_n(sm.data(), 3, smPos);
	}
	Pose::Compute(a_player, alpha, _state.faceAngle);   // Skyrim's upper-body animation on Mario for this frame
	if (!Pose::Mirroring()) a_player->SetPosition(pos, true);
	_lastSet = a_player->GetPosition();
	_lastPlace = place;
	_haveLastSet = true;
	if (auto controller = a_player->GetCharController()) controller->fallStartHeight = pos.z;
	// not the heading: Skyrim's third-person camera follows the player's heading, so turning the player with Mario
	// spins the camera and Mario runs in circles. Mario's mesh shows his own facing; the player's heading (and so
	// the camera and Skyrim's aiming) stays on the mouse.

	if (Render::Visible()) {
		// third person only: there's no first-person Mario
		Camera::EnsureThirdPerson();
		// the player's body comes back after loads and race/armour changes: keep it hidden
		SetPlayerVisible(a_player, false);
		// smooth: every frame; stepping (the old way): only when SM64 has a new tick
		if (Config::smoothMotion || ticked) {
			Render::Update(_geo, _prevGeoPos.data(), _prevTris, _state.position, _prevSm64, alpha, pos, Config::scale, _state.flags);
			if (auto root = Render::Root()) {
				Arms::Update(a_player, root->AsNode(), pos, alpha);
				// SM64's objects round Mario: the Power Star, coins
				Objects::Begin();
				Star::Put(alpha);
				if (auto camera = RE::PlayerCamera::GetSingleton(); camera && camera->cameraRoot) {
					const auto cam = ToSm64(camera->cameraRoot->world.translate);
					Coins::Put(cam.data());
				}
				Objects::End(root->AsNode(), smPos, Config::scale);
			}
		}
	}

	if (Config::telemetry) {
		// a frame where SM64 says Mario is moving but the player hasn't moved since last frame is a visible stall
		_statFrames++;
		const RE::NiPoint3 now = a_player->GetPosition();
		const float        moved = now.GetDistance(_statPrevPlayer);
		// Mario really moved in SM64 this frame (not a wall push) but the player's world position didn't
		if (ticked) _statMoving = std::hypot(_state.position[0] - sm64Before.x, _state.position[2] - sm64Before.z) > 1.0f;
		if (_statMoving && moved < 1e-3f) _statStalls++;
		_statPrevPlayer = now;
		if ((_statTimer += a_delta) >= 1.0f) {
			logger::info("STAT frames={} ticks={} stalls={} cost={:.3f}ms", _statFrames, _statTicks, _statStalls, _statCostMs / std::max(1, _statFrames));
			_statTimer = 0.0f;
			_statFrames = _statTicks = _statStalls = 0;
			_statCostMs = 0.0;
		}
	}
	if (Config::telemetry && (_telemetryTimer += a_delta) >= 0.1f) {
		_telemetryTimer = 0.0f;
		auto p = a_player->GetPosition();
		logger::info("TEL mario=({:.1f},{:.1f},{:.1f}) vel=({:.2f},{:.2f},{:.2f}) action=0x{:08X} health=0x{:04X} tris={} player=({:.1f},{:.1f},{:.1f}) drawn={} hp={:.0f}/{:.0f} camyaw={:.3f}",
			_state.position[0], _state.position[1], _state.position[2], _state.velocity[0], _state.velocity[1], _state.velocity[2],
			_state.action, static_cast<std::uint16_t>(_state.health), _geo.numTrianglesUsed, p.x, p.y, p.z, Render::TrianglesDrawn(),
			a_player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth), a_player->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth), CameraYaw());
	}

}

bool Mario::Respawn(RE::PlayerCharacter* a_player)
{
	const auto pos = a_player->GetPosition();
	_origin = pos;
	_streamed = false;
	StreamWorld(a_player, true);
	if (!_streamed) {
		// nothing read at all (the new cell's collision hasn't arrived): Skyrim's own rays say what ground is under the
		// player, and that is added; if there's none yet, try again next frame
		std::vector<RE::NiPoint3> pts{ pos };
		for (float r : { 60.0f, 140.0f, 260.0f })
			for (int i = 0; i < 8; i++) pts.push_back({ pos.x + std::cos(i * 0.785398f) * r, pos.y + std::sin(i * 0.785398f) * r, pos.z });
		if (Collision::Heal(a_player, pts, [](const RE::NiPoint3&) { return true; }, 70.0f) > 0) StreamWorld(a_player, true);
		if (!_streamed) return false;
	}
	// the new cell's collision can still be arriving, and the spot can lie under something (a gate's arch, a bridge, a mine's
	// roof): spawn only on ground at the player's feet, not the highest floor over them, and not into the void. Skyrim's
	// own physics says what's missing (a drawbridge that arrives late, say) and it's added; if the ground still isn't
	// there, wait a few seconds, then spawn anyway.
	float spawnFloor = 0.0f;
	if (Sm64::find_floor_height) {
		spawnFloor = GroundAtFeet(a_player, true);
		if (spawnFloor < -1e3f && ++_respawnWaits < 90) {
			if (Config::telemetry) logger::info("respawn: no ground at the player's feet yet, waiting");
			_streamed = false;
			return false;
		}
		if (Config::telemetry) logger::info("respawn: ground {:.0f} at the player's feet after {} waits", spawnFloor, _respawnWaits);
		if (spawnFloor < -1e3f) spawnFloor = 0.0f;
	}
	_respawnWaits = 0;
	const float spawnY = spawnFloor + 20.0f / Config::scale;
	Sm64::mario_delete(_id);
	_id = Sm64::mario_create(0.0f, spawnY, 0.0f);
	if (_id < 0) return false;
	Sm64::set_mario_faceangle(_id, RE::NI_PI - a_player->GetAngleZ());
	_state = {};
	_state.position[1] = spawnY;
	ResetPrev();
	_respawnPending = false;
	_haveLastSet = false;
	_haveSafe = false;
	_noFloorTicks = 0;
	_accum = 0.0f;
	Combat::Reset();
	logger::info("Mario followed the player to ({:.0f}, {:.0f}, {:.0f})", pos.x, pos.y, pos.z);
	return true;
}

float Mario::GroundAtFeet(RE::PlayerCharacter* a_player, bool a_heal)
{
	auto floorAt = [&]() {
		if (Sm64::lock) Sm64::lock();
		// SM64 takes floors up to 78 above the probe: probing at -30 lets ground up to ~48 above his feet count (a step, a
		// bump) and no more. The highest floor over him (an arch, a bridge, a roof) is not where he stands.
		const float f = Sm64::find_floor_height(0.0f, -30.0f, 0.0f);
		if (Sm64::unlock) Sm64::unlock();
		return f;
	};
	float f = floorAt();
	if (a_heal) {   // always: is something missing that Skyrim has under him (a bridge or gate sill the stream lacks, with terrain lower down still counting as a floor)?
		std::vector<RE::NiPoint3> pts{ _origin };
		for (float r : { 50.0f, 110.0f })
			for (int i = 0; i < 8; i++) pts.push_back({ _origin.x + std::cos(i * 0.785398f) * r, _origin.y + std::sin(i * 0.785398f) * r, _origin.z });
		const float scale = Config::scale;
		auto        missing = [&](const RE::NiPoint3& p) {
            auto sm = ToSm64(p);
            if (Sm64::lock) Sm64::lock();
            const float fl = Sm64::find_floor_height(sm[0], sm[1] + 60.0f, sm[2]);
            if (Sm64::unlock) Sm64::unlock();
            return fl < -1e4f || (sm[1] - fl) * scale > 10.0f;
		};
		if (Collision::Heal(a_player, pts, missing, 70.0f) > 0) {
			StreamWorld(a_player, true);
			f = floorAt();
		}
	}
	return f;
}

void Mario::Enable(RE::PlayerCharacter* a_player)
{
	_respawnPending = false;
	_haveLastSet = false;
	if (!Sm64::Ready()) {
		Notify("TES64: Super Mario 64 ROM not loaded (see TES64.log)");
		return;
	}
	_origin = a_player->GetPosition();
	_streamed = false;
	StreamWorld(a_player, true);
	if (!_streamed) {
		Notify("TES64: no collision found here");
		return;
	}

	// on the ground at the player's feet (not the highest floor over them: an arch, a bridge, a roof)
	float spawnFloor = Sm64::find_floor_height ? GroundAtFeet(a_player, true) : 0.0f;
	if (spawnFloor < -1e3f) spawnFloor = 0.0f;
	const float spawnY = spawnFloor + 20.0f / Config::scale;
	_id = Sm64::mario_create(0.0f, spawnY, 0.0f);
	if (_id < 0) {
		logger::error("sm64_mario_create failed: no floor under the player");
		Notify("TES64: Mario couldn't spawn here");
		return;
	}
	Sm64::set_mario_faceangle(_id, RE::NI_PI - a_player->GetAngleZ());

	constexpr std::size_t n = 3 * SM64_GEO_MAX_TRIANGLES;
	_geoPos.assign(3 * n, 0.0f);
	_geoNormal.assign(3 * n, 0.0f);
	_geoColor.assign(3 * n, 0.0f);
	_geoUv.assign(2 * n, 0.0f);
	_geo = { _geoPos.data(), _geoNormal.data(), _geoColor.data(), _geoUv.data(), 0 };
	_prevGeoPos.assign(_geoPos.size(), 0.0f);
	_state = {};
	_state.position[1] = spawnY;
	ResetPrev();
	_accum = 0.0f;
	Combat::Reset();

	if (auto controls = RE::ControlMap::GetSingleton()) controls->ToggleControls(kTakenControls, false, false);
	if (Render::Show()) {
		SetPlayerVisible(a_player, false);
		Camera::Apply(160.0f * Config::scale);   // Mario is ~160 SM64 units tall
	}
	// the hidden character's voice: TES64.esp's voice type with no lines, so its attack grunts and shouts of effort
	// don't come out of Mario
	if (auto base = a_player->GetActorBase(); base && !_savedVoice) {
		auto data = RE::TESDataHandler::GetSingleton();
		if (auto silent = data ? data->LookupForm<RE::BGSVoiceType>(0x801, "TES64.esp") : nullptr) {
			_savedVoice = base->voiceType ? base->voiceType : reinterpret_cast<RE::BGSVoiceType*>(-1);
			base->voiceType = silent;
		}
	}
	logger::info("Mario mode on at ({}, {}, {})", _origin.x, _origin.y, _origin.z);
	Notify("It's-a me, Mario!");
}

namespace
{
	// the player's own body hidden, geometry by geometry: the skeleton keeps animating, and spell hand effects
	// (under the magic nodes) and Mario (under the root) stay drawn
	void CullBody(RE::NiAVObject* a_obj, bool a_cull)
	{
		if (!a_obj || a_obj == Render::Root()) return;
		const std::string_view name = a_obj->name.c_str();
		if (name.find("MagicNode") != std::string_view::npos) return;
		if (a_obj->AsGeometry()) {
			a_obj->SetAppCulled(a_cull);
			return;
		}
		if (auto node = a_obj->AsNode()) {
			for (auto& c : node->GetChildren()) CullBody(c.get(), a_cull);
		}
	}
}

void Mario::SetPlayerVisible(RE::PlayerCharacter* a_player, bool a_visible)
{
	CullBody(a_player ? a_player->Get3D(false) : nullptr, !a_visible);
}

void Mario::Disable()
{
	if (_savedVoice) {
		if (auto player = RE::PlayerCharacter::GetSingleton(); player && player->GetActorBase())
			player->GetActorBase()->voiceType = _savedVoice == reinterpret_cast<RE::BGSVoiceType*>(-1) ? nullptr : _savedVoice;
		_savedVoice = nullptr;
	}
	Arms::Clear();
	Star::Reset();
	Coins::Reset();
	Objects::Hide();
	Pose::Reset();
	Render::Hide();
	Camera::Restore();
	SetPlayerVisible(RE::PlayerCharacter::GetSingleton(), true);
	if (auto player = RE::PlayerCharacter::GetSingleton(); player && _savedGravity) {
		if (auto controller = player->GetCharController()) {
			controller->gravity = *_savedGravity;
			controller->fallStartHeight = player->GetPositionZ();
			controller->fallTime = 0.0f;
		}
	}
	_savedGravity.reset();
	Sm64::mario_delete(_id);
	_id = -1;
	if (auto controls = RE::ControlMap::GetSingleton()) controls->ToggleControls(kTakenControls, true, false);
	logger::info("Mario mode off");
}

namespace
{
	constexpr float kStreamRadius = 2500.0f;   // Skyrim units of world kept in libsm64 around Mario
	constexpr float kStreamMove = 900.0f;      // reload once he's this far from the last load's centre
	constexpr float kMaxEdge = 3000.0f;        // SM64 units: libsm64 ignores very large triangles

	void PushTri(std::vector<SM64Surface>& a_out, const std::array<float, 3>& a, const std::array<float, 3>& b, const std::array<float, 3>& c, int a_depth = 0)
	{
		auto d2 = [](const std::array<float, 3>& p, const std::array<float, 3>& q) {
			return (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]);
		};
		if (a_depth < 6 && std::max({ d2(a, b), d2(b, c), d2(c, a) }) > kMaxEdge * kMaxEdge) {
			auto mid = [](const std::array<float, 3>& p, const std::array<float, 3>& q) {
				return std::array<float, 3>{ (p[0] + q[0]) / 2, (p[1] + q[1]) / 2, (p[2] + q[2]) / 2 };
			};
			auto ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
			PushTri(a_out, a, ab, ca, a_depth + 1);
			PushTri(a_out, ab, b, bc, a_depth + 1);
			PushTri(a_out, ca, bc, c, a_depth + 1);
			PushTri(a_out, ab, bc, ca, a_depth + 1);
			return;
		}
		auto r = [](float f) { return static_cast<std::int32_t>(std::lround(f)); };
		SM64Surface s{ 0, 0, 0, { { r(a[0]), r(a[1]), r(a[2]) }, { r(b[0]), r(b[1]), r(b[2]) }, { r(c[0]), r(c[1]), r(c[2]) } } };
		a_out.push_back(s);
	}
}

// Skyrim's collision round Mario, streamed into libsm64 as he moves (Collision.cpp reads the Havok world)
void Mario::StreamWorld(RE::PlayerCharacter* a_player, bool a_force)
{
	// the physics world, not the cell: outdoors the cell flips as the player crosses cell borders, the world doesn't
	auto cell = a_player->GetParentCell();
	auto world = cell ? cell->GetbhkWorld() : nullptr;
	auto pos = a_player->GetPosition();
	// a door near Mario swinging open or shut changes the collision: reload when one has moved
	static std::uint64_t s_doorSig = 0;
	static int           s_doorTick = 0;
	bool                 doorMoved = false;
	if (!a_force && _streamed && (s_doorTick++ % 9) == 0) {
		auto sig = Collision::DoorSignature(a_player, 1500.0f);
		doorMoved = sig != s_doorSig;
		s_doorSig = sig;
	}
	if (!a_force && !doorMoved && _streamed && world == _streamWorld && pos.GetDistance(_streamCenter) < kStreamMove) return;
	if (a_force) s_doorSig = Collision::DoorSignature(a_player, 1500.0f);

	std::vector<Collision::Tri> tris;
	auto stats = Collision::Gather(cell, pos, kStreamRadius, tris);
	if (tris.empty()) {
		if (!_streamed) logger::warn("collision: nothing found round the player ({} bodies)", stats.bodies);
		return;   // keep what's loaded rather than drop Mario through the world
	}
	std::vector<SM64Surface> surfaces;
	surfaces.reserve(tris.size());
	for (auto& t : tris) PushTri(surfaces, ToSm64(t.v[0]), ToSm64(t.v[1]), ToSm64(t.v[2]));
	Sm64::static_surfaces_load(surfaces.data(), static_cast<std::uint32_t>(surfaces.size()));

	_streamCenter = pos;
	_streamWorld = world;
	_streamed = true;
	if (Config::telemetry) {
		std::size_t floors = 0, walls = 0, ceils = 0;
		for (auto& f : surfaces) {
			auto& v = f.vertices;
			double ux = v[1][0] - v[0][0], uy = v[1][1] - v[0][1], uz = v[1][2] - v[0][2];
			double wx = v[2][0] - v[1][0], wy = v[2][1] - v[1][1], wz = v[2][2] - v[1][2];
			double nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
			double len = std::sqrt(nx * nx + ny * ny + nz * nz);
			if (len <= 0) continue;
			ny /= len;
			(ny > 0.01 ? floors : ny < -0.01 ? ceils : walls)++;
		}
		logger::info("collision{}: {} bodies ({} doors, {} moving, {} big hulls), {} triangles, {} surfaces ({} floors, {} walls, {} ceilings), {} unread shapes, {:.1f} ms",
			a_force ? " (enable)" : "", stats.bodies, stats.doors, stats.moving, stats.bigHulls, stats.tris, surfaces.size(), floors, walls, ceils, stats.unhandled, stats.ms);
	}
}

// Skyrim's own physics ray, straight down at each point of a grid round the player, against the floor SM64 found there:
// where the game's ground is above SM64's, collision went missing between Havok and libsm64
void Mario::RayGrid(RE::PlayerCharacter* a_player, float a_radius, float a_step)
{
	if (!Active() || !Sm64::find_floor_height) return;
	auto cell = a_player->GetParentCell();
	auto bhk = cell ? cell->GetbhkWorld() : nullptr;
	if (!bhk) return;
	const float s = RE::bhkWorld::GetWorldScale();
	const auto  me = a_player->GetPosition();
	int         total = 0, missing = 0;
	RE::BSReadLockGuard lock(bhk->worldLock);
	for (float dx = -a_radius; dx <= a_radius; dx += a_step)
		for (float dy = -a_radius; dy <= a_radius; dy += a_step) {
			const float x = me.x + dx, y = me.y + dy, top = me.z + 600.0f, bottom = me.z - 1500.0f;
			RE::bhkPickData pick;
			pick.rayInput.from = RE::hkVector4(x * s, y * s, top * s, 0.0f);
			pick.rayInput.to = RE::hkVector4(x * s, y * s, bottom * s, 0.0f);
			pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kCharController);
			if (!bhk->PickObject(pick) || !pick.rayOutput.HasHit()) continue;
			const float hitZ = top + (bottom - top) * pick.rayOutput.hitFraction;
			auto        sm = ToSm64({ x, y, hitZ });
			if (Sm64::lock) Sm64::lock();
			const float floor = Sm64::find_floor_height(sm[0], sm[1] + 60.0f, sm[2]);
			if (Sm64::unlock) Sm64::unlock();
			total++;
			const float gap = (sm[1] - floor) * Config::scale;   // Skyrim units SM64's floor is below the game's ground
			if (floor < -1e4f || gap > 30.0f) {
				missing++;
				auto layer = pick.rayOutput.rootCollidable ? static_cast<int>(pick.rayOutput.rootCollidable->GetCollisionLayer()) : -1;
				logger::info("RAYGAP at=({:.0f},{:.0f},{:.0f}) layer={} gap={:.0f}", x, y, hitZ, layer, floor < -1e4f ? 9999.0f : gap);
			}
		}
	logger::info("RAYGRID {} rays hit, {} where SM64's floor is over 30 units lower", total, missing);
}

void Mario::WaterGrid(RE::PlayerCharacter* a_player, float a_radius, float a_step)
{
	auto cell = a_player->GetParentCell();
	if (!cell) return;
	const auto me = a_player->GetPosition();
	int        claimed = 0, real = 0;
	for (float dx = -a_radius; dx <= a_radius; dx += a_step)
		for (float dy = -a_radius; dy <= a_radius; dy += a_step) {
			const RE::NiPoint3 p{ me.x + dx, me.y + dy, me.z };
			float              h;
			if (!cell->GetWaterHeight(p, h) || h < -1e6f || h > 1e6f) continue;
			claimed++;
			const float sub = a_player->GetSubmergeLevel(h - 30.0f, cell);   // would the game say something 30 units under that surface is wet?
			if (sub > 0.0f) real++;
			if (claimed % 10 == 1) logger::info("WATERPT at=({:.0f},{:.0f}) surface={:.0f} (player z {:.0f}) submerge={:.2f}", p.x, p.y, h, me.z, sub);
			// where the water is deep (the ground well under its surface): the ground by a ray down through the physics
			if (sub > 0.0f)
				if (auto bhk = cell->GetbhkWorld()) {
					const float     s = RE::bhkWorld::GetWorldScale();
					RE::bhkPickData pick;
					pick.rayInput.from = RE::hkVector4(p.x * s, p.y * s, (h + 300.0f) * s, 0.0f);
					pick.rayInput.to = RE::hkVector4(p.x * s, p.y * s, (h - 600.0f) * s, 0.0f);
					pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kCharController);
					if (bhk->PickObject(pick) && pick.rayOutput.HasHit()) {
						const float ground = (h + 300.0f) + ((h - 600.0f) - (h + 300.0f)) * pick.rayOutput.hitFraction;
						if (ground < h - 80.0f) logger::info("WATERDEEP at=({:.0f},{:.0f}) surface={:.0f} ground={:.0f}", p.x, p.y, h, ground);
					}
				}
		}
	logger::info("WATERGRID {} points where the cell claims water, {} where the game agrees", claimed, real);
}

// Every quarter second: rays down round Mario and ahead of him against SM64's floor. Whatever solid ground the game has
// that the stream doesn't (it can live in lists the stream doesn't read) is added, and the world reloads.
void Mario::HealCollision(RE::PlayerCharacter* a_player)
{
	if (!Sm64::find_floor_height) return;
	const auto                 me = a_player->GetPosition();
	std::vector<RE::NiPoint3> pts{ me };
	for (float r : { 110.0f, 260.0f })
		for (int i = 0; i < 8; i++) pts.push_back({ me.x + std::cos(i * 0.785398f) * r, me.y + std::sin(i * 0.785398f) * r, me.z });
	// where he's heading
	const float sp = std::hypot(_state.velocity[0], _state.velocity[2]);
	if (sp > 4.0f)
		for (float r : { 400.0f, 650.0f }) pts.push_back({ me.x + _state.velocity[0] / sp * r, me.y - _state.velocity[2] / sp * r, me.z });
	const float scale = Config::scale;
	auto        missing = [&](const RE::NiPoint3& p) {
        auto sm = ToSm64(p);
        if (Sm64::lock) Sm64::lock();
        const float floor = Sm64::find_floor_height(sm[0], sm[1] + 60.0f, sm[2]);
        if (Sm64::unlock) Sm64::unlock();
        return floor < -1e4f || (sm[1] - floor) * scale > 30.0f;
	};
	if (Collision::Heal(a_player, pts, missing) > 0) StreamWorld(a_player, true);
}

void Mario::DebugDropOut()
{
	if (!Active() || _id < 0) return;
	Sm64::set_mario_position(_id, _state.position[0], _state.position[1] - 30000.0f, _state.position[2]);
	logger::info("debug: Mario dropped out of the world");
}

void Mario::FloorScan(RE::PlayerCharacter* a_player, float a_radius)
{
	if (!Active() || !Sm64::find_floor_height) return;
	std::vector<Collision::BodyInfo> bodies;
	Collision::Bodies(a_player, a_radius, bodies);
	int bad = 0, total = 0;
	for (auto& b : bodies) {
		const float dx = b.mx.x - b.mn.x, dy = b.mx.y - b.mn.y, dz = b.mx.z - b.mn.z;
		if (!b.wanted || dz > 250.0f || std::max(dx, dy) < 200.0f) continue;   // slabs: decks, ramps, steps
		const RE::NiPoint3 top{ (b.mn.x + b.mx.x) / 2, (b.mn.y + b.mx.y) / 2, b.mx.z };
		auto               sm = ToSm64(top);
		if (Sm64::lock) Sm64::lock();
		const float floor = Sm64::find_floor_height(sm[0], sm[1] + 60.0f, sm[2]);
		if (Sm64::unlock) Sm64::unlock();
		const float gap = (sm[1] - floor) * Config::scale;   // Skyrim units the floor lies below the body's top
		total++;
		if (floor < -1e4f || gap > 25.0f) {
			bad++;
			logger::info("FLOORGAP layer={} shape={} tris={} box=({:.0f},{:.0f},{:.0f})..({:.0f},{:.0f},{:.0f}) gap={:.0f}", b.layer, b.shape, b.tris,
				b.mn.x, b.mn.y, b.mn.z, b.mx.x, b.mx.y, b.mx.z, floor < -1e4f ? 9999.0f : gap);
		}
	}
	logger::info("FLOORSCAN {} slabs, {} without a floor at their top", total, bad);
}

void Mario::UpdateWater(RE::PlayerCharacter* a_player)
{
	float level = -1e6f;
	auto  pos = a_player->GetPosition();
	if (auto cell = a_player->GetParentCell()) {
		float h;
		if (cell->GetWaterHeight(pos, h) && h > -1e6f && h < 1e6f) {
			// the cell's water height can be a stray number: an area where it reports a surface hundreds of units above the
			// ground with no water there (Whiterun's market), and Mario swims up into the air. The game's own answer to
			// "is something just under that surface wet?" is the truth: real water says yes, a phantom says no
			if (a_player->GetSubmergeLevel(h - 30.0f, cell) > 0.0f) level = h;
		}
	}
	auto sm = ToSm64({ pos.x, pos.y, level });
	// Mario is only in water if there's water under him: the ground he stands on (a bridge, the bank, a ledge) above
	// the surface means he's on land, however far the surface is below his feet. Otherwise the water reads as his
	// own, and SM64 swims him in the air.
	if (level > -1e6f && Sm64::find_floor_height) {
		if (Sm64::lock) Sm64::lock();
		const float floor = Sm64::find_floor_height(_state.position[0], _state.position[1] + 80.0f, _state.position[2]);
		if (Sm64::unlock) Sm64::unlock();
		if (Config::telemetry) {
			static int s_n = 0;
			if (s_n++ % 15 == 0) {
				auto  cellNow = a_player->GetParentCell();
				float ext = cellNow ? cellNow->GetExteriorWaterHeight() : -1.0f;
				logger::info("WATER level={:.0f} skyrimZ={:.0f} marioY={:.0f} floor={:.0f} -> {} (cellWater={:.0f} playerZ={:.0f} hasWaterFlag={} gameSwim={} gameInWater={} submerge={:.2f})", sm[1], level, _state.position[1], floor,
					floor > -1e4f && floor > sm[1] - 10.0f ? "land" : "water", ext, a_player->GetPosition().z,
					cellNow && cellNow->cellFlags.any(RE::TESObjectCELL::Flag::kHasWater) ? 1 : 0,
					a_player->AsActorState()->IsSwimming() ? 1 : 0, a_player->IsInWater() ? 1 : 0, a_player->GetSubmergeLevel(a_player->GetPosition().z, cellNow));
			}
		}
		if (floor > -1e4f && floor > sm[1] - 10.0f) level = -1e6f;
	}
	Sm64::set_mario_water_level(_id, static_cast<int>(level <= -1e6f ? -1000000 : sm[1]));
}

void Mario::ResetPrev()
{
	std::copy_n(_state.position, 3, _prevSm64);
	_prevTris = 0;
}

void Mario::Tick(RE::PlayerCharacter* a_player)
{
	auto input = Input::GetSingleton();
	SM64MarioInputs in{};

	if (auto controls = RE::PlayerControls::GetSingleton()) {
		in.stickX = controls->data.moveInputVec.x;    // right +
		in.stickY = -controls->data.moveInputVec.y;   // libsm64: back +
	}
	in.buttonA = input->a;
	in.buttonB = input->b;
	in.buttonZ = input->z;

	// SM64 steers relative to the camera: give it the camera-to-Mario direction
	in.camLookX = 0.0f;
	in.camLookZ = -1.0f;
	if (auto camera = RE::PlayerCamera::GetSingleton(); camera && camera->cameraRoot) {
		auto cam = ToSm64(camera->cameraRoot->world.translate);
		float dx = _state.position[0] - cam[0], dz = _state.position[2] - cam[2];
		if (dx * dx + dz * dz > 1e-4f) { in.camLookX = dx; in.camLookZ = dz; }
	}

	// Skyrim's furniture or killmove: Mario is where the player is, facing their way, and doesn't move on his own; his body
	// is the player's (Pose.cpp). Otherwise, Skyrim swinging, casting, blocking or drawing: his body turns to the crosshair
	// and his upper body plays Skyrim's animation (drawn by Render); SM64 still steers him with the stick, so he strafes.
	if (Pose::Mirroring()) {
		in = SM64MarioInputs{};
		input->dive = false;
		const auto sm = ToSm64(a_player->GetPosition());
		Sm64::set_mario_position(_id, sm[0], sm[1], sm[2]);
		Sm64::set_mario_velocity(_id, 0.0f, 0.0f, 0.0f);
		Sm64::set_mario_faceangle(_id, RE::NI_PI - a_player->GetAngleZ());
	}
	// the hidden player's own attacks still go to the crosshair, which is SM64's dive input's business too
	if (input->dive.exchange(false)) Sm64::set_mario_action(_id, 0x0188088A);   // ACT_DIVE (the pad's X tap)
	Caps::Tick(a_player, _id);
	if (Sm64::lock) Sm64::lock();
	// keep this tick's output as the one to blend from, before SM64 overwrites it
	std::copy_n(_geoPos.data(), _prevGeoPos.size(), _prevGeoPos.data());
	_prevTris = _geo.numTrianglesUsed;
	std::copy_n(_state.position, 3, _prevSm64);
	Sm64::mario_tick(_id, &in, &_state, &_geo);
	if (Sm64::unlock) Sm64::unlock();
	// the safety net: with no floor anywhere under him he is out of the world (a door or a quest moved something, a body
	// the stream lacks): back to the last solid footing, and the world re-read
	if (Sm64::find_floor_height && !Pose::Mirroring()) {
		if (Sm64::lock) Sm64::lock();
		const float under = Sm64::find_floor_height(_state.position[0], _state.position[1] + 80.0f, _state.position[2]);
		if (Sm64::unlock) Sm64::unlock();
		if (under > -1e4f) {
			_noFloorTicks = 0;
			if (!(_state.action & 0x00000800) && under > _state.position[1] - 30.0f) {   // standing on it
				std::copy_n(_state.position, 3, _safe);
				_haveSafe = true;
			}
		} else if (++_noFloorTicks == 15) {   // half a second of nothing
			logger::info("RESCUE Mario has no floor at ({:.0f},{:.0f},{:.0f}) in SM64 units: {}", _state.position[0], _state.position[1], _state.position[2],
				_haveSafe ? "back to his last footing" : "respawning");
			if (_haveSafe) {
				Sm64::set_mario_position(_id, _safe[0], _safe[1] + 20.0f, _safe[2]);
				Sm64::set_mario_velocity(_id, 0.0f, 0.0f, 0.0f);
				Sm64::set_mario_action(_id, 0x0C400201);   // idle
				ResetPrev();
			} else {
				_respawnPending = true;
			}
			_streamed = false;   // read the world again
			StreamWorld(a_player, true);
			_noFloorTicks = 0;
		}
	}
	Pose::OnTick(_state.position, Arms::PartCount());
	Arms::ReadHands([this](const float* p) { return ToSkyrim(p); }, RE::NI_PI - _state.faceAngle);
	Combat::Tick(a_player, _id, _state, ToSkyrim(_state.position), [this](const RE::NiPoint3& p) { return ToSm64(p); });
	Star::Tick(a_player, _id, _state);
	Coins::Tick(a_player, _state, [this](const RE::NiPoint3& p) { return ToSm64(p); });
}
