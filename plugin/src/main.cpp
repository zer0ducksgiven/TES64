#include "Combat.h"
#include "Hud.h"
#include "Caps.h"
#include "Config.h"
#include "Input.h"
#include "Mario.h"
#include "Render.h"
#include "Sm64.h"

namespace
{
	// [Debug] bTelemetry: menu opens/closes in the log, for the UAT harness
	class MenuLog : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event) logger::info("menu {} {}", a_event->opening ? "open" : "close", a_event->menuName.c_str());
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void InitLogging()
	{
		auto dir = logger::log_directory();
		if (!dir) return;
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((*dir / "TES64.log").string(), true);
		auto log = std::make_shared<spdlog::logger>("TES64", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == SKSE::MessagingInterface::kPostLoadGame || a_msg->type == SKSE::MessagingInterface::kNewGame) {
			logger::info("game loaded");
		}
		if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			if (Sm64::Init()) Render::WriteAtlas();
			Hud::InstallHook();
			Input::Register();
			Combat::Register();
			Caps::Register();
			if (Config::telemetry) {
				static MenuLog menuLog;
				RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&menuLog);
			}
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitLogging();
	SKSE::Init(a_skse);
	logger::info("TES64 loading");

	Config::Load();
	Mario::InstallHooks();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
