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

	std::atomic<bool> a{ false };   // Skyrim's Jump
	std::atomic<bool> b{ false };   // Config::attackKey / attackPad
	std::atomic<bool> z{ false };   // Skyrim's Sneak

	RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override;
};
