#include "Input.h"
#include "Config.h"
#include "Mario.h"

namespace
{
	// XInput button masks, as Skyrim reports them for a gamepad
	constexpr std::uint32_t kPadLeftStick = 0x0040, kPadLeftShoulder = 0x0100, kPadA = 0x1000, kPadY = 0x8000;
	constexpr float         kTap = 0.3f;   // seconds: shorter on X is a dive, longer is Skyrim's draw / sheathe

	bool s_aIgnored = false;   // this press of A was Skyrim's Activate (the crosshair was on something usable), not a jump

	// Would A activate something right now? Only what Skyrim itself would offer: within arm's reach (the crosshair picks far
	// things too), not an enemy, and nothing at all in a fight with weapons out, so combat never eats a jump.
	bool OffersInteraction()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto pick = RE::CrosshairPickData::GetSingleton();
		if (!player || !pick) return false;
		auto target = pick->GetActiveTarget().get();
		if (!target) return false;
		if (player->GetPosition().GetDistance(target->GetPosition()) > 220.0f) return false;   // Skyrim's prompt range
		if (player->IsInCombat() && player->AsActorState()->IsWeaponDrawn()) return false;
		if (auto actor = target->As<RE::Actor>(); actor && !actor->IsDead() && actor->IsHostileToActor(player)) return false;   // a foe isn't a conversation
		return true;
	}

	// Skyrim's Activate handler, for the pad's A while Mario is out: A is his jump unless it's activating something within
	// reach (OffersInteraction). A jump press would otherwise still make Skyrim try the crosshair's far target and play its
	// "can't do that" sound, so those presses are held back.
	struct ActivateHook
	{
		static void ProcessButton(RE::ActivateHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
		{
			static bool s_passed = true;   // this press of A went to Skyrim (so does its release)
			if (a_event && Mario::GetSingleton()->Active() && a_event->GetDevice() == RE::INPUT_DEVICE::kGamepad && a_event->GetIDCode() == kPadA) {
				if (a_event->IsDown()) s_passed = OffersInteraction();
				if (!s_passed) return;
			}
			original(a_this, a_event, a_data);
		}
		static inline REL::Relocation<decltype(ProcessButton)> original;
	};

	// Skyrim's Ready Weapon handler, for the pad's X while Mario is out: Skyrim would draw on the press, so the press is
	// held back. Let go quickly: Mario dives. Held: the weapons are drawn or put away, once.
	// Skyrim's Sprint handler: the left bumper (and the keyboard's sprint key) would make the hidden character sprint in the
	// background, draining stamina, while Mario crouches or long-jumps. There is no sprint as Mario: every press is held back.
	struct SprintHook
	{
		static void ProcessButton(RE::SprintHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
		{
			if (Config::telemetry && a_event && (a_event->IsDown() || a_event->IsUp())) logger::info("SPRINTHOOK {} device={} mario={}", a_event->IsDown() ? "down" : "up", static_cast<int>(a_event->GetDevice()), Mario::GetSingleton()->Active());
			if (Mario::GetSingleton()->Active()) {
				if (Config::telemetry && a_event && a_event->IsDown()) logger::info("SPRINT press held back (Mario is out)");
				return;
			}
			original(a_this, a_event, a_data);
		}
		static inline REL::Relocation<decltype(ProcessButton)> original;
	};

	struct ReadyWeaponHook
	{
		static void ProcessButton(RE::ReadyWeaponHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
		{
			if (!a_event || a_event->GetDevice() != RE::INPUT_DEVICE::kGamepad || !Mario::GetSingleton()->Active()) {
				original(a_this, a_event, a_data);
				return;
			}
			static bool s_fired = false;
			if (a_event->IsDown()) s_fired = false;
			if (a_event->IsPressed() && !s_fired && a_event->HeldDuration() >= kTap) {
				s_fired = true;
				if (auto player = RE::PlayerCharacter::GetSingleton()) player->DrawWeaponMagicHands(!player->AsActorState()->IsWeaponDrawn());
			}
			if (a_event->IsUp()) {
				if (!s_fired) Input::GetSingleton()->dive = true;
				s_fired = false;
			}
		}
		static inline REL::Relocation<decltype(ProcessButton)> original;
	};
}

void Input::SetSprintBound(bool a_bound)
{
	static std::vector<std::pair<RE::ControlMap::UserEventMapping*, std::uint16_t>> s_saved;
	auto map = RE::ControlMap::GetSingleton();
	if (!map) return;
	if (!a_bound && s_saved.empty()) {
		auto context = map->controlMap[RE::UserEvents::INPUT_CONTEXT_ID::kGameplay];
		if (!context) return;
		for (auto& list : context->deviceMappings)
			for (auto& m : list)
				if (m.eventID == "Sprint"sv) {
					s_saved.emplace_back(&m, m.inputKey);
					m.inputKey = 0xFF;   // no key
				}
		logger::info("controls: Sprint unbound ({} mappings)", s_saved.size());
	} else if (a_bound && !s_saved.empty()) {
		for (auto& [m, key] : s_saved) m->inputKey = key;
		logger::info("controls: Sprint bound again");
		s_saved.clear();
	}
}

void Input::Register()
{
	if (auto mgr = RE::BSInputDeviceManager::GetSingleton()) {
		mgr->AddEventSink(GetSingleton());
	}
	REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ReadyWeaponHandler[0] };
	ReadyWeaponHook::original = vtbl.write_vfunc(0x4, ReadyWeaponHook::ProcessButton);
	REL::Relocation<std::uintptr_t> sprint{ RE::VTABLE_SprintHandler[0] };
	SprintHook::original = sprint.write_vfunc(0x4, SprintHook::ProcessButton);
	REL::Relocation<std::uintptr_t> activate{ RE::VTABLE_ActivateHandler[0] };
	ActivateHook::original = activate.write_vfunc(0x4, ActivateHook::ProcessButton);
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
			// an Xbox pad, fixed whatever Skyrim's own bindings are: A jumps (unless it's activating something), Y punches /
			// kicks (SM64's B; B is Skyrim's menu), a tap of X dives and holding it draws / sheathes (ReadyWeaponHook), the
			// left bumper or the left stick's click crouches / ground-pounds (Z). The sticks, triggers (attack, cast), B,
			// the shouts and the d-pad stay Skyrim's.
			switch (code) {
			case kPadA:
				// A is also Skyrim's Activate: looking at something usable, it's that, not a jump
				if (button->IsDown()) s_aIgnored = OffersInteraction();
				a = pressed && !s_aIgnored;
				if (!pressed) s_aIgnored = false;
				break;
			case kPadY:
				b = pressed;
				break;
			case kPadLeftShoulder:
			case kPadLeftStick:
				z = pressed;
				break;
			default:
				break;
			}
			continue;
		}
		auto mapped = [&](std::string_view ev) { return controlMap && controlMap->GetMappedKey(ev, device) == code; };
		if (mapped("Jump"sv)) a = pressed;
		else if (mapped("Sneak"sv)) z = pressed;
		else if (device == RE::INPUT_DEVICE::kKeyboard && code == Config::attackKey)
			b = pressed;
	}
	return RE::BSEventNotifyControl::kContinue;
}
