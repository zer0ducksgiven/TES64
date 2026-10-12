#include "Input.h"
#include "Config.h"
#include "Mario.h"

namespace
{
	// XInput button masks, as Skyrim reports them for a gamepad
	constexpr std::uint32_t kPadLeftStick = 0x0040, kPadLeftShoulder = 0x0100, kPadA = 0x1000, kPadX = 0x4000;
	constexpr float         kTap = 0.3f;   // seconds: shorter on X is a dive, longer is Skyrim's draw / sheathe
}

void Input::Register()
{
	if (auto mgr = RE::BSInputDeviceManager::GetSingleton()) {
		mgr->AddEventSink(GetSingleton());
	}
}

RE::BSEventNotifyControl Input::ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*)
{
	if (!a_event) return RE::BSEventNotifyControl::kContinue;
	auto controlMap = RE::ControlMap::GetSingleton();
	auto ui = RE::UI::GetSingleton();
	bool inMenu = ui && ui->GameIsPaused();

	for (auto e = *a_event; e; e = e->next) {
		auto button = e->AsButtonEvent();
		if (!button) continue;
		auto device = button->GetDevice();
		auto code = button->GetIDCode();
		bool pressed = button->IsPressed();

		if (device == RE::INPUT_DEVICE::kKeyboard && code == Config::toggleKey && button->IsDown() && !inMenu) {
			Mario::GetSingleton()->RequestToggle();
			continue;
		}
		if (device != RE::INPUT_DEVICE::kKeyboard && device != RE::INPUT_DEVICE::kGamepad) continue;

		if (device == RE::INPUT_DEVICE::kGamepad) {
			// an Xbox pad, fixed whatever Skyrim's own bindings are: A jumps, Y punches / kicks (SM64's B; B is Skyrim's
			// menu), a tap of X dives (holding X stays Skyrim's draw / sheathe), the left bumper or the left stick's click
			// crouches / ground-pounds (Z). The sticks, triggers (attack, cast), B, the shouts and the d-pad stay Skyrim's.
			switch (code) {
			case kPadA:
				a = pressed;
				break;
			case kPadX:
				if (button->IsUp() && button->HeldDuration() < kTap) dive = true;
				break;
			case kPadLeftShoulder:
			case kPadLeftStick:
				z = pressed;
				break;
			default:
				if (code == Config::attackPad) b = pressed;
				break;
			}
			continue;
		}
		auto mapped = [&](std::string_view ev) { return controlMap && controlMap->GetMappedKey(ev, device) == code; };
		if (mapped("Jump"sv)) a = pressed;
		else if (mapped("Sneak"sv)) z = pressed;
		else if ((device == RE::INPUT_DEVICE::kKeyboard && code == Config::attackKey) ||
				 (device == RE::INPUT_DEVICE::kGamepad && code == Config::attackPad))
			b = pressed;
	}
	return RE::BSEventNotifyControl::kContinue;
}
