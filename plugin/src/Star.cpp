#include "Star.h"
#include "Config.h"
#include "Objects.h"

#include <random>

namespace
{

	// SM64 actions and sounds (decomp sm64.h, audio_defines.h)
	constexpr std::uint32_t ACT_STAR_DANCE_NO_EXIT = 0x00001307, ACT_STAR_DANCE_WATER = 0x00001303, ACT_FALL_AFTER_STAR_GRAB = 0x00001904;
	constexpr std::uint32_t ACT_FLAG_AIR = 0x00000800, ACT_FLAG_SWIMMING = 0x00002000, ACT_FLAG_METAL_WATER = 0x00004000;
	constexpr std::uint32_t Sound(std::uint32_t a_bank, std::uint32_t a_flags, std::uint32_t a_id, std::uint32_t a_prio, std::uint32_t a_f2 = 8)
	{
		return (a_bank << 28) | (a_flags << 24) | (a_id << 16) | (a_prio << 8) | (a_f2 << 4) | 1u;
	}
	constexpr std::uint32_t SOUND_MENU_STAR_SOUND = Sound(7, 0, 0x1E, 0xFF), SOUND_GENERAL_STAR_APPEARS = Sound(3, 0, 0x57, 0xFF, 9);
	// SM64's sequences, as NG64 plays them: the collect jingle and the star-appears jingle
	constexpr std::uint8_t kSeqCollectStar = 0x01, kSeqStarSpawn = 0x15;

	bool s_ready = false;   // the star's model came from the ROM (Objects)

	enum class Phase { kNone, kFlying, kWaiting, kCelebrating };
	Phase s_phase = Phase::kNone;
	int   s_timer = 0;
	float s_from[3], s_home[3];
	float s_pos3[3], s_prev3[3];          // SM64 units, this tick and the last
	float s_yaw = 0.0f, s_prevYaw = 0.0f; // radians
	float s_scale = 1.0f;

	int   s_quests = -1;     // completed quests last counted
	float s_questTimer = 0.0f;
	bool  s_dancing = false;
	bool  s_pending = false;

	std::mt19937 s_rng{ 64 };
	float Random() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(s_rng); }

	int CountCompleted()
	{
		auto data = RE::TESDataHandler::GetSingleton();
		if (!data) return 0;
		int n = 0;
		for (auto quest : data->GetFormArray<RE::TESQuest>())
			if (quest && quest->IsCompleted()) n++;
		return n;
	}

	// a spot a few metres from Mario on open ground: a floor near his height, nothing above for a while, no wall
	// within the star's reach
	bool FindSpot(const float a_mario[3], float a_out[3])
	{
		if (!Sm64::find_floor_height || !Sm64::find_wall_collision) return false;
		for (int tries = 0; tries < 24; tries++) {
			const float ang = Random() * RE::NI_TWO_PI, dist = 250.0f + Random() * 250.0f;   // ~1.2-2.4 m at 0.48
			float       x = a_mario[0] + std::sin(ang) * dist, z = a_mario[2] + std::cos(ang) * dist;
			if (Sm64::lock) Sm64::lock();
			const float floor = Sm64::find_floor_height(x, a_mario[1] + 300.0f, z);
			float       y = floor + 160.0f;
			float       wx = x, wy = y, wz = z;
			const int   walls = Sm64::find_wall_collision(&wx, &wy, &wz, 0.0f, 90.0f);
			SM64SurfaceCollisionData* ceil = nullptr;
			const float               ceilY = Sm64::find_ceil ? Sm64::find_ceil(x, floor + 20.0f, z, &ceil) : 1e9f;
			if (Sm64::unlock) Sm64::unlock();
			if (floor < -1e4f || std::abs(floor - a_mario[1]) > 300.0f) continue;   // a drop, a cliff, nothing loaded
			if (walls > 0 || (ceil && ceilY < floor + 320.0f)) continue;
			a_out[0] = x;
			a_out[1] = y;
			a_out[2] = z;
			return true;
		}
		return false;
	}

	void Spawn(const float a_mario[3])
	{
		float home[3];
		if (!FindSpot(a_mario, home)) {
			logger::info("star: no open ground near Mario, trying again in a moment");
			s_pending = true;
			return;
		}
		s_pending = false;
		std::copy_n(a_mario, 3, s_from);
		s_from[1] += 100.0f;
		std::copy_n(home, 3, s_home);
		std::copy_n(s_from, 3, s_pos3);
		std::copy_n(s_from, 3, s_prev3);
		s_phase = Phase::kFlying;
		s_timer = 0;
		s_scale = 1.0f;
		if (Config::sounds && Sm64::music_play) Sm64::music_play(kSeqStarSpawn);
		if (Config::telemetry) logger::info("STAR spawn from=({:.0f},{:.0f},{:.0f}) home=({:.0f},{:.0f},{:.0f})", s_from[0], s_from[1], s_from[2], home[0], home[1], home[2]);
	}
}

void Star::Init() { s_ready = Objects::Ready(); }

void Star::Reset()
{
	s_phase = Phase::kNone;
	s_dancing = false;
	s_pending = false;
	s_quests = -1;
}

void Star::SpawnNow() { s_pending = true; }
bool Star::Dancing() { return s_dancing; }

void Star::Tick(RE::PlayerCharacter* a_player, std::int32_t a_mario, const SM64MarioState& a_state)
{
	if (!s_ready) return;
	std::copy_n(s_pos3, 3, s_prev3);
	s_prevYaw = s_yaw;

	// a quest completed: a star (counted every couple of seconds; the first count only sets where we start from)
	if ((s_questTimer -= 1.0f / 30.0f) <= 0.0f) {
		s_questTimer = 2.0f;
		const int n = CountCompleted();
		if (s_quests >= 0 && n > s_quests) s_pending = true;
		s_quests = n;
	}
	if (s_pending && s_phase == Phase::kNone && !(a_state.action & ACT_FLAG_AIR)) Spawn(a_state.position);

	const auto& m = a_state.position;
	s_dancing = a_state.action == ACT_STAR_DANCE_NO_EXIT || a_state.action == ACT_STAR_DANCE_WATER || a_state.action == ACT_FALL_AFTER_STAR_GRAB;
	switch (s_phase) {
	case Phase::kNone:
		break;
	case Phase::kFlying:
		{
			// bhvStarSpawnCoordinates, simplified: out of Mario in an arc to its place, spinning, then it settles
			const float t = std::min(1.0f, ++s_timer / 40.0f);
			for (int i = 0; i < 3; i++) s_pos3[i] = s_from[i] + (s_home[i] - s_from[i]) * t;
			s_pos3[1] += std::sin(t * RE::NI_PI) * 400.0f;
			s_yaw += 0.8f;
			if (s_timer >= 40) {
				s_phase = Phase::kWaiting;
				s_timer = 0;
				if (Config::sounds && Sm64::play_sound) Sm64::play_sound(SOUND_GENERAL_STAR_APPEARS);
			}
			break;
		}
	case Phase::kWaiting:
		{
			s_yaw += 0.2f;   // 0x800 a tick, as a waiting star turns
			s_pos3[1] = s_home[1] + std::sin(++s_timer * 0.1f) * 8.0f;
			// touched: SM64's star hitbox (80 radius, 50 high) against Mario's (37 radius, 160 high)
			const float dx = m[0] - s_pos3[0], dz = m[2] - s_pos3[2];
			if (dx * dx + dz * dz < 117.0f * 117.0f && m[1] + 160.0f > s_pos3[1] - 50.0f && m[1] < s_pos3[1] + 50.0f) {
				std::uint32_t grab = ACT_STAR_DANCE_NO_EXIT;
				if (a_state.action & (ACT_FLAG_SWIMMING | ACT_FLAG_METAL_WATER)) grab = ACT_STAR_DANCE_WATER;
				if (a_state.action & ACT_FLAG_AIR) grab = ACT_FALL_AFTER_STAR_GRAB;
				if (Config::sounds && Sm64::play_sound) Sm64::play_sound(SOUND_MENU_STAR_SOUND);
				if (Sm64::mario_heal) Sm64::mario_heal(a_mario, 31);
				if (Sm64::set_mario_action_arg) Sm64::set_mario_action_arg(a_mario, grab, 1);
				else Sm64::set_mario_action(a_mario, grab);
				if (Config::sounds && Sm64::music_play) Sm64::music_play(kSeqCollectStar);
				// Skyrim's health back up too, as SM64's star heals
				auto av = a_player->AsActorValueOwner();
				av->RestoreActorValue(RE::ActorValue::kHealth, av->GetPermanentActorValue(RE::ActorValue::kHealth));
				s_phase = Phase::kCelebrating;
				s_timer = 0;
				if (Config::telemetry) logger::info("STAR collected action=0x{:08X}", grab);
			}
			break;
		}
	case Phase::kCelebrating:
		{
			// bhvCelebrationStar: it circles Mario as he dances, rising, then shrinks away
			++s_timer;
			const float ang = s_timer * 0.4f, r = 70.0f;
			s_pos3[0] = m[0] + std::sin(ang) * r;
			s_pos3[2] = m[2] + std::cos(ang) * r;
			s_pos3[1] = m[1] + 40.0f + std::min(s_timer, 40) * 4.0f;
			s_yaw = ang + RE::NI_HALF_PI;
			s_scale = s_timer < 60 ? 0.6f : std::max(0.0f, 0.6f - (s_timer - 60) * 0.05f);
			if ((s_timer > 40 && !s_dancing) || s_scale <= 0.0f) {
				s_phase = Phase::kNone;
			}
			break;
		}
	}
	if (s_phase == Phase::kFlying && s_timer == 1) std::copy_n(s_pos3, 3, s_prev3);
}

void Star::Put(float a_alpha)
{
	if (s_phase == Phase::kNone || !s_ready) return;
	float p[3];
	for (int i = 0; i < 3; i++) p[i] = s_prev3[i] + (s_pos3[i] - s_prev3[i]) * a_alpha;
	Objects::Put(Objects::StarMesh(), p, s_prevYaw + (s_yaw - s_prevYaw) * a_alpha, s_scale);
}
