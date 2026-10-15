#include "Camera.h"
#include "Config.h"

#include <unordered_map>

namespace
{
	std::unordered_map<std::string, float> s_saved;
	std::optional<float>                   s_savedZoom;

	RE::ThirdPersonState* ThirdPerson()
	{
		auto camera = RE::PlayerCamera::GetSingleton();
		if (!camera) return nullptr;
		auto& state = camera->GetRuntimeData().cameraStates[RE::CameraState::kThirdPerson];
		return static_cast<RE::ThirdPersonState*>(state.get());
	}

	void Set(const char* a_name, float a_value)
	{
		auto ini = RE::INISettingCollection::GetSingleton();
		auto setting = ini ? ini->GetSetting(a_name) : nullptr;
		if (!setting) {
			logger::warn("camera: no setting {}", a_name);
			return;
		}
		s_saved.try_emplace(a_name, setting->GetFloat());
		setting->SetFloat(a_value);
	}
}

bool Camera::EnsureThirdPerson()
{
	auto camera = RE::PlayerCamera::GetSingleton();
	auto cur = camera ? camera->currentState.get() : nullptr;
	if (cur && cur->id == RE::CameraState::kFirstPerson) {
		camera->ForceThirdPerson();
		return true;
	}
	// zooming in all the way would put the camera inside Mario: hold a minimum distance (zooming out is free)
	if (auto tps = ThirdPerson(); tps && cur == tps && tps->targetZoomOffset < Config::minCameraZoom) {
		tps->targetZoomOffset = Config::minCameraZoom;
	}
	return false;
}

void Camera::Apply(float a_marioHeight)
{
	EnsureThirdPerson();
	// the camera looks at the player's head height; Mario's head is lower by this much
	const float drop = -std::max(0.0f, 120.0f - a_marioHeight);
	Set("fOverShoulderPosX:Camera", 0.0f);
	Set("fOverShoulderCombatPosX:Camera", 0.0f);
	Set("fOverShoulderPosZ:Camera", drop);
	Set("fOverShoulderCombatPosZ:Camera", drop + Config::combatCameraLift);
	Set("fOverShoulderHorseAddY:Camera", 0.0f);

	// pulled back far enough to see him: fully zoomed in, the camera sits on top of Mario
	if (auto tps = ThirdPerson()) {
		if (!s_savedZoom) s_savedZoom = tps->targetZoomOffset;
		tps->targetZoomOffset = std::max(tps->targetZoomOffset, Config::minCameraZoom);
		tps->currentZoomOffset = std::max(tps->currentZoomOffset, Config::minCameraZoom);
		tps->savedZoomOffset = std::max(tps->savedZoomOffset, Config::minCameraZoom);
	}
	if (auto camera = RE::PlayerCamera::GetSingleton()) {
		auto cur = camera->currentState.get();
		auto tps = ThirdPerson();
		logger::info("camera: state id {} (third person is {}), zoom target {:.2f} current {:.2f}",
			cur ? static_cast<int>(cur->id) : -1, static_cast<int>(RE::CameraState::kThirdPerson),
			tps ? tps->targetZoomOffset : -1.0f, tps ? tps->currentZoomOffset : -1.0f);
	}
}

void Camera::Restore()
{
	auto ini = RE::INISettingCollection::GetSingleton();
	for (auto& [name, value] : s_saved) {
		if (auto setting = ini ? ini->GetSetting(name) : nullptr) setting->SetFloat(value);
	}
	s_saved.clear();
	if (auto tps = ThirdPerson(); tps && s_savedZoom) tps->targetZoomOffset = *s_savedZoom;
	s_savedZoom.reset();
}
