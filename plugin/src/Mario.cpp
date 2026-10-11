#include "Mario.h"
#include "Config.h"
#include "Input.h"
#include "Collision.h"

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

void Mario::Update(RE::PlayerCharacter* a_player, float a_delta)
{
	if (Config::telemetry && !_seenUpdate) {
		_seenUpdate = true;
		logger::info("player update running");
	}
	if (_toggleRequested.exchange(false)) {
		if (Active()) Disable();
		else Enable(a_player);
	}
	if (!Active()) return;

	auto ui = RE::UI::GetSingleton();
	if (ui && ui->GameIsPaused()) return;   // menus stop Mario with the game

	_accum += a_delta;
	if (_accum > 0.25f) _accum = kTick;   // after a hitch, don't fast-forward
	while (_accum >= kTick) {
		_accum -= kTick;
		StreamWorld(a_player, false);
		UpdateWater(a_player);
		Tick(a_player);
	}

	// the player follows Mario: position, and heading (SM64 yaw 0 faces +Z, which is Skyrim's south).
	// Skyrim's own physics stays out of it: no gravity pulling the player under Mario mid-jump, no fall damage
	// on top of Mario's own
	if (auto controller = a_player->GetCharController()) {
		if (!_savedGravity) _savedGravity = controller->gravity;
		controller->gravity = 0.0f;
		controller->fallTime = 0.0f;
		controller->SetLinearVelocityImpl(RE::hkVector4(0.0f, 0.0f, 0.0f, 0.0f));
	}
	auto pos = ToSkyrim(_state.position);
	a_player->SetPosition(pos, true);
	if (auto controller = a_player->GetCharController()) controller->fallStartHeight = pos.z;
	a_player->SetAngle({ 0.0f, 0.0f, RE::NI_PI - _state.faceAngle });

	if (Config::telemetry && (_telemetryTimer += a_delta) >= 0.1f) {
		_telemetryTimer = 0.0f;
		auto p = a_player->GetPosition();
		logger::info("TEL mario=({:.1f},{:.1f},{:.1f}) vel=({:.2f},{:.2f},{:.2f}) action=0x{:08X} health=0x{:04X} tris={} player=({:.1f},{:.1f},{:.1f})",
			_state.position[0], _state.position[1], _state.position[2], _state.velocity[0], _state.velocity[1], _state.velocity[2],
			_state.action, static_cast<std::uint16_t>(_state.health), _geo.numTrianglesUsed, p.x, p.y, p.z);
	}

}

void Mario::Enable(RE::PlayerCharacter* a_player)
{
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

	_id = Sm64::mario_create(0.0f, 20.0f / Config::scale, 0.0f);
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
	_accum = 0.0f;

	if (auto controls = RE::ControlMap::GetSingleton()) controls->ToggleControls(kTakenControls, false, false);
	logger::info("Mario mode on at ({}, {}, {})", _origin.x, _origin.y, _origin.z);
	Notify("It's-a me, Mario!");
}

void Mario::Disable()
{
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
	if (!a_force && _streamed && world == _streamWorld && pos.GetDistance(_streamCenter) < kStreamMove) return;

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
		logger::info("collision{}: {} bodies, {} triangles, {} surfaces ({} floors, {} walls, {} ceilings), {} unread shapes, {:.1f} ms",
			a_force ? " (enable)" : "", stats.bodies, stats.tris, surfaces.size(), floors, walls, ceils, stats.unhandled, stats.ms);
	}
}

void Mario::UpdateWater(RE::PlayerCharacter* a_player)
{
	float level = -1e6f;
	auto  pos = a_player->GetPosition();
	if (auto cell = a_player->GetParentCell()) {
		float h;
		if (cell->GetWaterHeight(pos, h) && h > -1e6f && h < 1e6f) level = h;
	}
	auto sm = ToSm64({ pos.x, pos.y, level });
	Sm64::set_mario_water_level(_id, static_cast<int>(level <= -1e6f ? -1000000 : sm[1]));
}

void Mario::Tick(RE::PlayerCharacter*)
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

	Sm64::mario_tick(_id, &in, &_state, &_geo);
}
