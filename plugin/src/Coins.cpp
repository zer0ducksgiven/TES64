#include "Coins.h"
#include "Config.h"
#include "Objects.h"

#include <random>

namespace
{
	constexpr int   kMaxCoins = 20;        // per body: more gold makes each coin worth more
	constexpr float kLifetime = 120.0f;    // seconds a coin waits (then its gold goes back into the body)
	constexpr std::uint32_t SOUND_GENERAL_COIN = (3u << 28) | (8u << 24) | (0x11u << 16) | (0x80u << 8) | (8u << 4) | 1u;

	struct Coin
	{
		float               pos[3];         // SM64 units
		int                 value;          // gold
		bool                blue;
		float               age = 0.0f, phase;
		RE::ObjectRefHandle body;
	};
	std::vector<Coin> s_coins;

	std::mutex                       s_lock;
	std::vector<RE::ObjectRefHandle> s_deaths;   // from the event, handled on Mario's tick
	std::mt19937                     s_rng{ 1985 };
	float Random() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(s_rng); }

	RE::TESBoundObject* Gold() { return RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F); }

	class DeathSink : public RE::BSTEventSink<RE::TESDeathEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
		{
			if (a_event && a_event->dead && a_event->actorDying && a_event->actorDying.get() != RE::PlayerCharacter::GetSingleton()) {
				std::scoped_lock l(s_lock);
				s_deaths.push_back(a_event->actorDying->CreateRefHandle());
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void GiveBack(const Coin& a_coin)
	{
		auto gold = Gold();
		auto body = a_coin.body.get();
		if (gold && body) body->AddObjectToContainer(gold, nullptr, a_coin.value, nullptr);
	}

	// a body's gold out as coins in a ring round it, floating a little above the ground
	void Spill(RE::Actor* a_body, const std::function<std::array<float, 3>(const RE::NiPoint3&)>& a_toSm64)
	{
		auto gold = Gold();
		if (!gold) return;
		auto counts = a_body->GetInventoryCounts([gold](RE::TESBoundObject& a_obj) { return &a_obj == gold; });
		auto it = counts.find(gold);
		const int total = it != counts.end() ? it->second : 0;
		if (total <= 0) return;
		a_body->RemoveItem(gold, total, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		const auto centre = a_toSm64(a_body->GetPosition());
		const int  n = std::min(total, kMaxCoins);
		for (int i = 0; i < n; i++) {
			Coin c{};
			c.value = total / n + (i < total % n ? 1 : 0);
			c.blue = c.value >= 5;
			const float ang = (i + Random() * 0.5f) / n * RE::NI_TWO_PI, r = 90.0f + Random() * 110.0f;
			c.pos[0] = centre[0] + std::sin(ang) * r;
			c.pos[2] = centre[2] + std::cos(ang) * r;
			c.pos[1] = centre[1];
			if (Sm64::find_floor_height) {
				if (Sm64::lock) Sm64::lock();
				const float floor = Sm64::find_floor_height(c.pos[0], centre[1] + 200.0f, c.pos[2]);
				if (Sm64::unlock) Sm64::unlock();
				if (floor > -1e4f && std::abs(floor - centre[1]) < 400.0f) c.pos[1] = floor;
			}
			c.pos[1] += 50.0f;
			c.phase = Random() * 6.28f;
			c.body = a_body->CreateRefHandle();
			s_coins.push_back(c);
		}
		if (Config::telemetry) logger::info("COINS {} gold from {:08X} as {} coins at=({:.0f},{:.0f},{:.0f})", total, a_body->GetFormID(), n, centre[0], centre[1], centre[2]);
	}
}

void Coins::Register()
{
	static DeathSink sink;
	if (auto events = RE::ScriptEventSourceHolder::GetSingleton()) events->AddEventSink<RE::TESDeathEvent>(&sink);
}

void Coins::Reset()
{
	for (auto& c : s_coins) GiveBack(c);
	s_coins.clear();
	std::scoped_lock l(s_lock);
	s_deaths.clear();
}

void Coins::Tick(RE::PlayerCharacter* a_player, const SM64MarioState& a_state, const std::function<std::array<float, 3>(const RE::NiPoint3&)>& a_toSm64)
{
	std::vector<RE::ObjectRefHandle> deaths;
	{
		std::scoped_lock l(s_lock);
		deaths.swap(s_deaths);
	}
	for (auto& h : deaths) {
		auto ref = h.get();
		auto actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (actor && actor->GetPosition().GetDistance(a_player->GetPosition()) < 6000.0f) Spill(actor, a_toSm64);
	}

	auto        gold = Gold();
	const auto& m = a_state.position;
	for (auto it = s_coins.begin(); it != s_coins.end();) {
		it->age += 1.0f / 30.0f;
		// SM64's coin hitbox (100 radius, 64 high) against Mario (37 wide, 160 tall)
		const float dx = m[0] - it->pos[0], dz = m[2] - it->pos[2];
		const bool  touched = dx * dx + dz * dz < 137.0f * 137.0f && m[1] + 160.0f > it->pos[1] && m[1] < it->pos[1] + 64.0f;
		if (touched && gold) {
			a_player->AddObjectToContainer(gold, nullptr, it->value, nullptr);
			if (Config::sounds && Sm64::play_sound) Sm64::play_sound(SOUND_GENERAL_COIN);
			if (Config::telemetry) logger::info("COIN +{} gold", it->value);
			it = s_coins.erase(it);
		} else if (it->age > kLifetime) {
			GiveBack(*it);
			it = s_coins.erase(it);
		} else {
			++it;
		}
	}
}

void Coins::Put(const float a_camera[3])
{
	static float s_time = 0.0f;
	s_time += 1.0f / 60.0f;
	for (auto& c : s_coins) {
		// facing the camera, its four spin pictures going round, bobbing
		const float yaw = std::atan2(a_camera[0] - c.pos[0], a_camera[2] - c.pos[2]);
		const int   frame = static_cast<int>((s_time * 12.0f + c.phase * 2.0f)) & 3;
		const float p[3] = { c.pos[0], c.pos[1] + std::sin(s_time * 3.0f + c.phase) * 6.0f, c.pos[2] };
		Objects::Put(Objects::CoinMesh(c.blue, frame), p, yaw, 1.0f);
	}
}
