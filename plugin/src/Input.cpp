#include "Input.h"
#include "Config.h"
#include "Mario.h"

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

		auto mapped = [&](std::string_view ev) { return controlMap && controlMap->GetMappedKey(ev, device) == code; };
		if (mapped("Jump"sv)) a = pressed;
		else if (mapped("Sneak"sv)) z = pressed;
		else if ((device == RE::INPUT_DEVICE::kKeyboard && code == Config::attackKey) ||
				 (device == RE::INPUT_DEVICE::kGamepad && code == Config::attackPad))
			b = pressed;
	}
	return RE::BSEventNotifyControl::kContinue;
}
