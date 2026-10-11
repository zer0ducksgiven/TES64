#include "Mario.h"
#include "Config.h"
#include "Input.h"

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
	LoadGround(a_player);

	_id = Sm64::mario_create(0.0f, 0.0f, 0.0f);
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

// Phase 1: a flat floor at the player's feet. Phase 2 replaces this with the world's Havok collision.
// Tiled: libsm64 can't find a floor in triangles much over ~16000 units across.
void Mario::LoadGround(RE::PlayerCharacter*)
{
	constexpr std::int32_t tile = 4096, n = 8;   // 32768 units square
	std::vector<SM64Surface> floor;
	for (std::int32_t i = -n / 2; i < n / 2; i++) {
		for (std::int32_t j = -n / 2; j < n / 2; j++) {
			std::int32_t x0 = i * tile, x1 = x0 + tile, z0 = j * tile, z1 = z0 + tile;
			floor.push_back({ 0, 0, 0, { { x0, 0, z0 }, { x0, 0, z1 }, { x1, 0, z1 } } });
			floor.push_back({ 0, 0, 0, { { x0, 0, z0 }, { x1, 0, z1 }, { x1, 0, z0 } } });
		}
	}
	Sm64::static_surfaces_load(floor.data(), static_cast<std::uint32_t>(floor.size()));
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
