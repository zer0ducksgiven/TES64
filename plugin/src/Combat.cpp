#include "Combat.h"
#include "Config.h"

namespace
{
	// SM64 actions (decomp include/sm64.h)
	enum : std::uint32_t
	{
		ACT_PUNCHING = 0x00800380,
		ACT_MOVE_PUNCHING = 0x00000389,
		ACT_JUMP_KICK = 0x018008AC,
		ACT_DIVE = 0x0188088A,
		ACT_DIVE_SLIDE = 0x00880456,
		ACT_SLIDE_KICK = 0x018008AA,
		ACT_SLIDE_KICK_SLIDE = 0x0080045A,
		ACT_GROUND_POUND = 0x008008A9,
		ACT_GROUND_POUND_LAND = 0x0080023C,
		ACT_FLAG_AIR = 0x00000800,
	};

	struct Attack
	{
		const char* kind;
		float       damage;     // Skyrim health
		float       reach;      // Skyrim units in front of Mario where the hit lands
		float       radius;
		bool        knockdown;
	};

	const Attack* AttackFor(std::uint32_t a_action)
	{
		static const Attack punch{ "punch", 10.0f, 40.0f, 45.0f, false };
		static const Attack kick{ "kick", 16.0f, 40.0f, 50.0f, true };
		static const Attack dive{ "dive", 14.0f, 25.0f, 45.0f, true };
		static const Attack slide{ "slidekick", 14.0f, 25.0f, 45.0f, true };
		static const Attack pound{ "groundpound", 30.0f, 0.0f, 80.0f, true };
		switch (a_action) {
		case ACT_PUNCHING:
		case ACT_MOVE_PUNCHING:
			return &punch;
		case ACT_JUMP_KICK:
			return &kick;
		case ACT_DIVE:
		case ACT_DIVE_SLIDE:
			return &dive;
		case ACT_SLIDE_KICK:
		case ACT_SLIDE_KICK_SLIDE:
			return &slide;
		case ACT_GROUND_POUND_LAND:
			return &pound;
		default:
			return nullptr;
		}
	}

	// one hit per target per attack: an attack is (action, animation) - the punch combo's third hit is a kick
	std::uint64_t                          s_attackId = 0;
	std::uint32_t                          s_lastAction = 0;
	std::int32_t                           s_lastAnim = -1;
	std::vector<RE::ObjectRefHandle>       s_hitThisAttack;
	std::int16_t                           s_lastWedges = 8;
	bool                                   s_injected = false;   // our own knockback took wedges: not SM64 damage
	bool                                   s_suppressNext = false, s_suppressCurrent = false;

	struct PendingHit
	{
		RE::NiPoint3 from;
		float        damage;
	};
	std::mutex              s_lock;
	std::vector<PendingHit> s_pending;

	class HitSink : public RE::BSTEventSink<RE::TESHitEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
		{
			auto player = RE::PlayerCharacter::GetSingleton();
			if (!a_event || !player) return RE::BSEventNotifyControl::kContinue;
			if (Config::telemetry && a_event->cause.get() == player && a_event->target) {
				// Skyrim's own hit (a weapon, spell or shout) landing for the player
				logger::info("SKYHIT target={:08X} source={:08X}", a_event->target->GetFormID(), a_event->source);
			}
			if (a_event->target.get() != player) return RE::BSEventNotifyControl::kContinue;
			auto cause = a_event->cause.get();
			if (!cause || cause == player) return RE::BSEventNotifyControl::kContinue;
			std::scoped_lock l(s_lock);
			s_pending.push_back({ cause->GetPosition(), 0.0f });
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	bool AlreadyHit(RE::TESObjectREFR* a_ref)
	{
		auto h = a_ref->CreateRefHandle();
		for (auto& x : s_hitThisAttack)
			if (x == h) return true;
		s_hitThisAttack.push_back(h);
		return false;
	}

	float MaxHealth(RE::Actor* a)
	{
		auto av = a->AsActorValueOwner();
		return std::max(1.0f, av->GetPermanentActorValue(RE::ActorValue::kHealth));
	}

	float Health(RE::Actor* a) { return a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth); }

	RE::bhkRigidBody* DynamicBody(RE::TESObjectREFR* a_ref)
	{
		auto obj = a_ref->Get3D();
		auto co = obj ? obj->GetCollisionObject() : nullptr;
		auto body = co ? skyrim_cast<RE::bhkRigidBody*>(co->body.get()) : nullptr;
		auto hk = body ? static_cast<RE::hkpRigidBody*>(body->referencedObject.get()) : nullptr;
		if (!hk) return nullptr;
		switch (hk->collidable.GetCollisionLayer()) {
		case RE::COL_LAYER::kClutter:
		case RE::COL_LAYER::kDebrisSmall:
		case RE::COL_LAYER::kDebrisLarge:
		case RE::COL_LAYER::kWeapon:
			return body;
		default:
			return nullptr;
		}
	}

	void Shove(RE::bhkRigidBody* a_body, const RE::NiPoint3& a_dir, float a_speed)
	{
		// game units/s -> Havok
		const float s = RE::bhkWorld::GetWorldScale();
		a_body->SetLinearVelocity(RE::hkVector4(a_dir.x * a_speed * s, a_dir.y * a_speed * s, a_dir.z * a_speed * s, 0.0f));
	}
}

void Combat::Register()
{
	static HitSink sink;
	if (auto events = RE::ScriptEventSourceHolder::GetSingleton()) {
		events->AddEventSink<RE::TESHitEvent>(&sink);
	}
}

void Combat::SuppressNextAttack() { s_suppressNext = true; }

void Combat::Reset()
{
	s_suppressNext = s_suppressCurrent = false;
	s_lastAction = 0;
	s_lastAnim = -1;
	s_hitThisAttack.clear();
	s_lastWedges = 8;
	s_injected = false;
	std::scoped_lock l(s_lock);
	s_pending.clear();
}

void Combat::Tick(RE::PlayerCharacter* a_player, std::int32_t a_mario, const SM64MarioState& a_state,
	const RE::NiPoint3& a_marioPos, const std::function<std::array<float, 3>(const RE::NiPoint3&)>& a_toSm64)
{
	// ---- health: Skyrim's is the truth -------------------------------------------------------------------------
	const std::int16_t wedges = static_cast<std::int16_t>(a_state.health >> 8);
	if (wedges < s_lastWedges && !s_injected && !a_player->IsDead()) {
		// SM64 hurt Mario (a fall, fire...): the same share of Skyrim health
		const float dmg = (s_lastWedges - wedges) * MaxHealth(a_player) / 8.0f;
		a_player->DoDamage(dmg, nullptr, true);
		if (Config::telemetry) logger::info("HURT sm64 wedges {} -> {}: {:.0f} health", s_lastWedges, wedges, dmg);
	}
	s_injected = false;
	if (a_player->IsDead()) {
		if (wedges > 0) Sm64::mario_kill(a_mario);
	} else {
		const float frac = std::clamp(Health(a_player) / MaxHealth(a_player), 0.0f, 1.0f);
		const auto  want = static_cast<std::int16_t>(std::clamp(static_cast<int>(std::ceil(frac * 8.0f)), 1, 8));
		Sm64::set_mario_health(a_mario, static_cast<std::uint16_t>((want << 8) | 0x80));
		s_lastWedges = want;
	}

	// ---- Skyrim hit the player: SM64 knockback away from the attacker ------------------------------------------
	{
		std::scoped_lock l(s_lock);
		for (auto& h : s_pending) {
			// SM64's invincibility after a hit: Skyrim's damage still lands, the knockback doesn't repeat
			if (a_state.invincTimer > 0) continue;
			auto p = a_toSm64(h.from);
			Sm64::mario_take_damage(a_mario, 1, 0, p[0], p[1], p[2]);
			Sm64::set_mario_invincibility(a_mario, 45);
			s_injected = true;
			if (Config::telemetry) logger::info("KNOCKBACK from ({:.0f},{:.0f},{:.0f})", h.from.x, h.from.y, h.from.z);
		}
		s_pending.clear();
	}

	// ---- Mario hits Skyrim -------------------------------------------------------------------------------------
	if (a_state.action != s_lastAction || a_state.animID != s_lastAnim) {
		s_lastAction = a_state.action;
		s_lastAnim = a_state.animID;
		s_hitThisAttack.clear();
		s_attackId++;
		if (AttackFor(a_state.action)) {
			s_suppressCurrent = s_suppressNext;
			s_suppressNext = false;
		} else {
			s_suppressCurrent = false;
		}
	}
	const float          heading = RE::NI_PI - a_state.faceAngle;
	const RE::NiPoint3   fwd{ std::sin(heading), std::cos(heading), 0.0f };
	const Attack*        attack = s_suppressCurrent ? nullptr : AttackFor(a_state.action);
	const float          scale = Config::scale / 0.48f;   // hit sizes were tuned at the default Mario scale

	// a stomp: falling onto someone's head
	const bool falling = (a_state.action & ACT_FLAG_AIR) && a_state.velocity[1] < -10.0f;

	auto tes = RE::TES::GetSingleton();
	if (!tes) return;
	tes->ForEachReferenceInRange(a_player, 250.0f * scale, [&](RE::TESObjectREFR* a_ref) {
		if (!a_ref || a_ref == a_player || a_ref->IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;
		auto pos = a_ref->GetPosition();

		if (auto actor = a_ref->As<RE::Actor>()) {
			if (actor->IsDead()) return RE::BSContainer::ForEachResult::kContinue;
			float height = actor->GetHeight();
			if (!(height > 30.0f)) height = 128.0f;   // some actors report no height
			const float dxy = std::hypot(pos.x - a_marioPos.x, pos.y - a_marioPos.y);
			const bool  bodyRange = a_marioPos.z > pos.z - 20.0f && a_marioPos.z < pos.z + height;
			const Attack* hit = nullptr;
			Attack        stomp{ "stomp", 18.0f, 0.0f, 40.0f, false };
			if (attack) {
				auto c = a_marioPos + fwd * (attack->reach * scale);
				// + the target's own width: Skyrim actors are wider than SM64's enemies
				if (std::hypot(pos.x - c.x, pos.y - c.y) < (attack->radius * scale + 35.0f) && bodyRange) hit = attack;
			}
			if (!hit && falling && dxy < 40.0f * scale && a_marioPos.z > pos.z + height * 0.7f && a_marioPos.z < pos.z + height + 30.0f) {
				hit = &stomp;
				Sm64::set_mario_velocity(a_mario, a_state.velocity[0], 40.0f, a_state.velocity[2]);   // bounce off
			}
			if (attack && !hit && Config::telemetry && s_hitThisAttack.empty()) {
				logger::info("MISS {} actor={:08X} dxy={:.0f} dz={:.0f} h={:.0f} at=({:.0f},{:.0f},{:.0f})", attack->kind, actor->GetFormID(), dxy, a_marioPos.z - pos.z, height, pos.x, pos.y, pos.z);
			}
			if (!hit || AlreadyHit(actor)) return RE::BSContainer::ForEachResult::kContinue;

			const float before = Health(actor);
			const float dmg = hit->damage * Config::damageMult;
			actor->DoDamage(dmg, a_player, false);
			if (!actor->IsHostileToActor(a_player) && !actor->IsDead()) actor->StartCombat(a_player);
			if (auto proc = a_player->GetActorRuntimeData().currentProcess; hit->knockdown && proc) {
				proc->KnockExplosion(actor, a_marioPos, 1.0f);
			}
			if (Config::telemetry) {
				logger::info("HIT {} target={:08X} dmg={:.0f} health {:.0f} -> {:.0f}", hit->kind, actor->GetFormID(), dmg, before, Health(actor));
			}
			return RE::BSContainer::ForEachResult::kContinue;
		}

		// physics clutter: shoved by attacks, nudged by running into it
		auto body = DynamicBody(a_ref);
		if (!body) return RE::BSContainer::ForEachResult::kContinue;
		RE::NiPoint3 d{ pos.x - a_marioPos.x, pos.y - a_marioPos.y, 0.0f };
		const float  dist = d.Length();
		if (dist < 1e-3f) d = fwd;
		else d /= dist;
		if (attack && dist < (attack->reach + attack->radius) * scale + 30.0f && std::abs(pos.z - a_marioPos.z) < 80.0f && !AlreadyHit(a_ref)) {
			Shove(body, RE::NiPoint3{ d.x, d.y, 0.6f }, attack->knockdown ? 700.0f : 450.0f);
			if (Config::telemetry) logger::info("SHOVE {} ref={:08X} dist={:.0f}", attack->kind, a_ref->GetFormID(), dist);
		} else if (!attack && dist < 30.0f * scale && std::abs(a_state.forwardVelocity) > 8.0f && std::abs(pos.z - a_marioPos.z) < 60.0f) {
			Shove(body, RE::NiPoint3{ d.x, d.y, 0.2f }, 120.0f);
		}
		return RE::BSContainer::ForEachResult::kContinue;
	});
}
