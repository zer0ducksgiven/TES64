#pragma once

// Raw button state for Mario's A/B/Z, plus the Mario-mode toggle. Movement comes from PlayerControls' own move vector.
class Input : public RE::BSTEventSink<RE::InputEvent*>
{
public:
	static Input* GetSingleton()
	{
		static Input singleton;
		return &singleton;
	}
	static void Register();
	// Skyrim's Sprint taken off every device while Mario is out (restored after): no sprint, so none of its camera effects
	static void SetSprintBound(bool a_bound);

	std::atomic<bool> a{ false };   // Skyrim's Jump
	std::atomic<bool> b{ false };   // Config::attackKey; the pad's Y
	std::atomic<bool> z{ false };   // Skyrim's Sneak
	std::atomic<bool> dive{ false };   // the pad's X, tapped: one dive (Mario consumes it)

	RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override;
};
