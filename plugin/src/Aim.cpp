#include "Aim.h"
#include "Config.h"

#include <unordered_set>

namespace
{
	bool                              s_hit = false;    // the last crosshair ray met something (telemetry)
	std::unordered_set<std::uint32_t> s_done;   // projectile handles already turned
	float                             s_prune = 0.0f;

	// where the crosshair points: the first thing the camera's centre ray meets past Mario, or far down the ray
	bool CrosshairPoint(RE::PlayerCharacter* a_player, RE::NiPoint3& a_target)
	{
		auto camera = RE::PlayerCamera::GetSingleton();
		if (!camera || !camera->cameraRoot) return false;
		const auto& root = camera->cameraRoot->world;
		const RE::NiPoint3 origin = root.translate;
		RE::NiPoint3       fwd{ root.rotate.entry[0][1], root.rotate.entry[1][1], root.rotate.entry[2][1] };
		const float        len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
		if (len < 0.5f) return false;
		fwd = fwd / len;
		const float past = std::max(0.0f, (a_player->GetPosition() - origin).Dot(fwd)) + 40.0f;   // beyond Mario
		const RE::NiPoint3 from = origin + fwd * past;
		const float kRange = 6000.0f;
		a_target = origin + fwd * kRange;
		s_hit = false;
		auto cell = a_player->GetParentCell();
		auto bhk = cell ? cell->GetbhkWorld() : nullptr;
		if (!bhk) return true;
		const float         s = RE::bhkWorld::GetWorldScale();
		RE::BSReadLockGuard lock(bhk->worldLock);
		RE::bhkPickData     pick;
		pick.rayInput.from = RE::hkVector4(from.x * s, from.y * s, from.z * s, 0.0f);
		pick.rayInput.to = RE::hkVector4(a_target.x * s, a_target.y * s, a_target.z * s, 0.0f);
		pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kProjectile);
		if (bhk->PickObject(pick) && pick.rayOutput.HasHit()) {
			a_target = from + (a_target - from) * pick.rayOutput.hitFraction;
			s_hit = true;
		}
		return true;
	}
}

void Aim::Reset()
{
	s_done.clear();
	s_prune = 0.0f;
}

void Aim::Tick(RE::PlayerCharacter* a_player)
{
	auto mgr = RE::Projectile::Manager::GetSingleton();
	if (!mgr) return;
	RE::NiPoint3 target;
	bool         have = false;
	auto         handleOf = [&](auto& a_list) {
		for (auto& h : a_list) {
			auto sp = h.get();
			auto proj = sp.get();
			if (!proj) continue;
			auto& rd = proj->GetProjectileRuntimeData();
			if (rd.shooter.get().get() != a_player) continue;
			// a shot is turned once; a stream (Flames, Sparks) is one projectile that lives as long as the cast, so it follows
			// the crosshair every frame
			const bool fresh = s_done.insert(h.native_handle()).second;
			if (!fresh && !proj->IsFlameProjectile() && !proj->IsBeamProjectile()) continue;
			if (!have) {
				if (!CrosshairPoint(a_player, target)) return;
				have = true;
			}
			const auto  pos = proj->GetPosition();
			RE::NiPoint3 dir = target - pos;
			const float  len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
			if (len < 50.0f) continue;
			dir = dir / len;
			const float speed = std::sqrt(rd.linearVelocity.x * rd.linearVelocity.x + rd.linearVelocity.y * rd.linearVelocity.y + rd.linearVelocity.z * rd.linearVelocity.z);
			if (speed < 1.0f) continue;
			const float oldPitch = std::atan2(rd.linearVelocity.z, std::hypot(rd.linearVelocity.x, rd.linearVelocity.y));
			rd.linearVelocity = dir * speed;
			rd.velocity = dir * (std::sqrt(rd.velocity.x * rd.velocity.x + rd.velocity.y * rd.velocity.y + rd.velocity.z * rd.velocity.z));
			proj->SetAngle({ -std::asin(dir.z), 0.0f, std::atan2(dir.x, dir.y) });
			if (Config::telemetry && !fresh) {
				static int s_n = 0;
				if (s_n++ % 30 == 0) {
					// the flame's own node: where it is drawn, against where the velocity sends it
					float npitch = 0.0f, nyaw = 0.0f, npos[3] = { 0, 0, 0 };
					if (auto node = proj->Get3D()) {
						const auto& r = node->world.rotate;
						const RE::NiPoint3 f{ r.entry[0][1], r.entry[1][1], r.entry[2][1] };
						npitch = std::asin(std::clamp(f.z, -1.0f, 1.0f));
						nyaw = std::atan2(f.x, f.y);
						npos[0] = node->world.translate.x; npos[1] = node->world.translate.y; npos[2] = node->world.translate.z;
					}
					logger::info("AIM stream: pitch {:.3f} yaw {:.3f} toward ({:.0f},{:.0f},{:.0f}) {:.0f} away, ray {} | from ({:.0f},{:.0f},{:.0f}) | node fwd pitch {:.3f} yaw {:.3f} at ({:.0f},{:.0f},{:.0f})", std::asin(dir.z), std::atan2(dir.x, dir.y), target.x, target.y, target.z, len, s_hit ? "hit" : "open sky", pos.x, pos.y, pos.z, npitch, nyaw, npos[0], npos[1], npos[2]);
				}
			}
			if (Config::telemetry && fresh) logger::info("AIM projectile turned: pitch {:.3f} -> {:.3f} toward ({:.0f},{:.0f},{:.0f}) {:.0f} away", oldPitch, std::asin(dir.z), target.x, target.y, target.z, len);
		}
	};
	handleOf(mgr->unlimited);
	handleOf(mgr->limited);
	if (s_done.size() > 400) s_done.clear();
}
