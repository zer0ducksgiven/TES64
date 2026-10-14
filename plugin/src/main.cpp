#include "Combat.h"
#include "Hud.h"
#include "Caps.h"
#include "Config.h"
#include "Fonts.h"
#include "Input.h"
#include "Mario.h"
#include "Render.h"
#include "Sm64.h"
#include "Coins.h"
#include "Objects.h"
#include "Star.h"
#include "Settings.h"

namespace
{
	// menu opens/closes in the log ([Debug] bTelemetry, for the UAT harness); MCM settings re-read when the
	// Journal (where SkyUI's MCM lives) closes
	class MenuLog : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (!a_event) return RE::BSEventNotifyControl::kContinue;
			if (!a_event->opening && a_event->menuName == RE::JournalMenu::MENU_NAME) Config::LoadMcm();
			Hud::MenuEvent(a_event->menuName.c_str(), a_event->opening);
			// the UAT harness: tutorial popups pause the game and nobody is there to answer them, so telemetry builds close them
			if (Config::telemetry && a_event->opening && a_event->menuName == RE::TutorialMenu::MENU_NAME)
				if (auto queue = RE::UIMessageQueue::GetSingleton()) queue->AddMessage(RE::TutorialMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			if (Config::telemetry) logger::info("menu {} {}", a_event->opening ? "open" : "close", a_event->menuName.c_str());
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
			if (Sm64::Init()) {
				Render::WriteAtlas();
				Objects::Init();
				Star::Init();
			}
			Hud::InstallHook();
			Input::Register();
			Combat::Register();
			Coins::Register();
			Caps::Register();
			static MenuLog menuLog;
			RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&menuLog);
			if (auto data = RE::TESDataHandler::GetSingleton()) {
				const bool mcm = data->LookupForm(0x800, "TES64.esp") != nullptr;
				logger::info("mcm: TES64.esp {}", mcm ? "loaded (its menu needs SkyUI)" : "not loaded");
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
	Fonts::Apply();   // before the UI reads fontconfig.txt
	Mario::InstallHooks();
	Settings::Register();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
