#include "Caps.h"
#include "Config.h"
#include "Sm64.h"

namespace
{
	struct Pending
	{
		std::uint32_t cap;
		float         seconds;
	};
	std::mutex           s_lock;
	std::vector<Pending> s_pending;

	void Queue(std::uint32_t a_cap, float a_seconds)
	{
		std::scoped_lock l(s_lock);
		s_pending.push_back({ a_cap, a_seconds });
	}

	// which effect instances already gave their cap (by spell + unique id: the ActiveEffect may be reused)
	std::vector<std::pair<RE::FormID, std::uint16_t>> s_granted;

	// what a cap-giving effect looks like (spell, potion or shout alike)
	std::uint32_t CapFor(RE::ActiveEffect* a_effect)
	{
		auto base = a_effect->GetBaseObject();
		if (!base) return 0;
		if (a_effect->spell && a_effect->spell->GetFormID() >= 0x0002F7BE && a_effect->spell->GetFormID() <= 0x0002F7C0) return Caps::kWing;
		if (base->GetArchetype() == RE::EffectSetting::Archetype::kInvisibility && a_effect->duration > 0.0f) return Caps::kVanish;
		// the armour spells (and armour potions/scrolls) - not the passive abilities and worn-armour bonuses that
		// also raise DamageResist
		const auto type = a_effect->spell->GetSpellType();
		const bool cast = type == RE::MagicSystem::SpellType::kSpell || type == RE::MagicSystem::SpellType::kScroll ||
		                  type == RE::MagicSystem::SpellType::kPotion;
		if (cast && a_effect->duration > 0.0f && base->data.primaryAV == RE::ActorValue::kDamageResist && !base->IsHostile()) return Caps::kMetal;
		return 0;
	}

	class CastSink : public RE::BSTEventSink<RE::TESSpellCastEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event, RE::BSTEventSource<RE::TESSpellCastEvent>*) override
		{
			if (Config::telemetry && a_event) logger::info("CAST {:08X} by {:08X}", a_event->spell, a_event->object ? a_event->object->GetFormID() : 0);
			if (!a_event || a_event->object.get() != RE::PlayerCharacter::GetSingleton()) return RE::BSEventNotifyControl::kContinue;
			// Whirlwind Sprint 1-3 (VoiceWhirlwindSprint1..3)
			if (a_event->spell >= 0x0002F7BE && a_event->spell <= 0x0002F7C0) Queue(Caps::kWing, 60.0f);
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

void Caps::Register()
{
	static CastSink casts;
	if (auto events = RE::ScriptEventSourceHolder::GetSingleton()) {
		events->AddEventSink<RE::TESSpellCastEvent>(&casts);
	}
}

void Caps::Tick(RE::PlayerCharacter* a_player, std::int32_t a_mario)
{
	// the player's active effects, twice a second
	static int s_poll = 0;
	if (a_player && ++s_poll >= 15) {
		s_poll = 0;
		std::vector<std::pair<RE::FormID, std::uint16_t>> live;
		std::uint32_t                                     grantedNow = 0;
		if (auto list = a_player->AsMagicTarget()->GetActiveEffectList()) {
			for (auto effect : *list) {
				if (!effect || !effect->spell) continue;
				const std::uint32_t cap = CapFor(effect);
				if (!cap) continue;
				std::pair<RE::FormID, std::uint16_t> key{ effect->spell->GetFormID(), effect->usUniqueID };
				live.push_back(key);
				if (std::find(s_granted.begin(), s_granted.end(), key) != s_granted.end()) continue;
				if (grantedNow & cap) continue;   // one spell can carry several such effects (Oakflesh: four)
				grantedNow |= cap;
				const float left = cap == kWing ? 60.0f : std::max(5.0f, effect->duration - effect->elapsedSeconds);
				Queue(cap, left);
			}
		}
		s_granted = std::move(live);
	}

	std::vector<Pending> pending;
	{
		std::scoped_lock l(s_lock);
		pending.swap(s_pending);
	}
	for (auto& p : pending) {
		if (a_mario < 0) continue;
		if (Sm64::lock) Sm64::lock();
		Sm64::mario_interact_cap(a_mario, p.cap, static_cast<std::uint16_t>(p.seconds * 30.0f), 0);
		if (Sm64::unlock) Sm64::unlock();
		if (Config::telemetry) logger::info("CAP {} for {:.0f} s", p.cap == kWing ? "wing" : p.cap == kMetal ? "metal" : "vanish", p.seconds);
	}
}
