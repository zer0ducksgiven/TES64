#include "Pose.h"
#include "Config.h"

#include <array>

namespace
{
	struct V3
	{
		float x = 0, y = 0, z = 0;
	};
	V3    operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	V3    operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	V3    operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	V3    Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	float Len(V3 a) { return std::sqrt(Dot(a, a)); }
	V3    Norm(V3 a, V3 a_fallback)
	{
		const float l = Len(a);
		return l > 1e-4f ? a * (1.0f / l) : a_fallback;
	}

	// 3x3, row-major, row vectors: v * M. Rows are where the basis vectors go.
	using M3 = std::array<float, 9>;
	M3 Mul(const M3& a, const M3& b)
	{
		M3 c{};
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				for (int k = 0; k < 3; k++) c[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
		return c;
	}
	V3 Apply(V3 v, const M3& m)
	{
		return { v.x * m[0] + v.y * m[3] + v.z * m[6], v.x * m[1] + v.y * m[4] + v.z * m[7], v.x * m[2] + v.y * m[5] + v.z * m[8] };
	}
	// the rotation that turns unit a onto unit b (a * R = b)
	M3 FromTo(V3 a, V3 b)
	{
		const V3    v = Cross(a, b);
		const float c = Dot(a, b);
		M3          rc{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };   // column-vector form first, transposed at the end
		if (c < -0.9999f) {                           // opposite: half a turn about anything perpendicular
			const V3 axis = Norm(Cross(a, std::abs(a.x) < 0.9f ? V3{ 1, 0, 0 } : V3{ 0, 1, 0 }), V3{ 0, 0, 1 });
			const float u[3] = { axis.x, axis.y, axis.z };
			for (int i = 0; i < 3; i++)
				for (int j = 0; j < 3; j++) rc[i * 3 + j] = 2 * u[i] * u[j] - (i == j ? 1.0f : 0.0f);
			return rc;   // symmetric
		}
		const float k = 1.0f / (1.0f + c);
		const M3    vx{ 0, -v.z, v.y, v.z, 0, -v.x, -v.y, v.x, 0 };
		const M3    vx2 = Mul(vx, vx);
		for (int i = 0; i < 9; i++) rc[i] += vx[i] + k * vx2[i];
		M3 r{};
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++) r[i * 3 + j] = rc[j * 3 + i];
		return r;
	}

	// ---- state ---------------------------------------------------------------------------------------------------
	const unsigned char* (*s_triParts)() = nullptr;
	const float* (*s_localPos)() = nullptr;
	const float* (*s_localNrm)() = nullptr;
	const float* (*s_partMatrix)(int) = nullptr;
	bool s_resolved = false;

	float s_cur[16][16], s_prev[16][16];
	float s_posCur[3], s_posPrev[3];
	bool  s_layoutOk = false;   // Mario has his usual 16 parts this tick

	bool  s_acting = false;
	float s_tail = 0.0f;
	float s_weight = 0.0f;                  // the larger of the two arms'
	float s_wRight = 0.0f, s_wLeft = 0.0f;  // how much of Skyrim's pose each arm shows
	float s_tailRight = 0.0f, s_tailLeft = 0.0f;
	bool  s_actRight = false, s_actLeft = false;
	bool  s_active = false;     // the pose is showing this frame

	Pose::Arm s_right, s_left;
	bool      s_rightOk = false, s_leftOk = false;

	void Resolve()
	{
		if (s_resolved) return;
		HMODULE dll = ::GetModuleHandleW(L"sm64.dll");
		if (!dll) return;
		s_triParts = reinterpret_cast<const unsigned char* (*)()>(::GetProcAddress(dll, "tes64_tri_parts"));
		s_localPos = reinterpret_cast<const float* (*)()>(::GetProcAddress(dll, "tes64_local_positions"));
		s_localNrm = reinterpret_cast<const float* (*)()>(::GetProcAddress(dll, "tes64_local_normals"));
		s_partMatrix = reinterpret_cast<const float* (*)(int)>(::GetProcAddress(dll, "tes64_part_matrix"));
		s_resolved = s_triParts && s_localPos && s_localNrm && s_partMatrix;
	}

	V3 Translation(const float (&a_m)[16]) { return { a_m[12], a_m[13], a_m[14] }; }
	M3 Rotation(const float (&a_m)[16])
	{
		return { a_m[0], a_m[1], a_m[2], a_m[4], a_m[5], a_m[6], a_m[8], a_m[9], a_m[10] };
	}
	// Skyrim world (x east, y north, z up) -> SM64 (x, y up, z): the plugin's usual mapping
	V3 ToSm64(const RE::NiPoint3& a_p) { return { a_p.x, a_p.z, -a_p.y }; }

	// which arms Skyrim is using: a swing is the right arm's (the left only joins for a block, a spell, a draw or sheathe),
	// so Mario's other arm keeps swinging with his own run and jump
	void ArmActions(RE::PlayerCharacter* a_player, bool& a_right, bool& a_left)
	{
		const auto state = a_player->AsActorState()->GetWeaponState();
		const bool moving = state == RE::WEAPON_STATE::kDrawing || state == RE::WEAPON_STATE::kSheathing;
		const bool casting = a_player->WhoIsCasting() != 0;
		const bool blocking = a_player->IsBlocking();
		a_right = a_player->IsAttacking() || casting || moving || blocking;
		a_left = casting || moving || blocking;
	}

	// two rotations blended and made orthonormal again
	void Ease(float& a_w, bool a_on, float a_dt)
	{
		const float target = a_on ? 1.0f : 0.0f;
		a_w += (target - a_w) * std::min(1.0f, a_dt * 14.0f);
		if (std::abs(target - a_w) < 0.01f) a_w = target;
	}

	// one arm: the parts (shoulder, forearm, hand) at a_first.., Skyrim's bones for that side
	bool ComputeArm(RE::NiAVObject* a_root, int a_first, const char* a_upper, const char* a_fore, const char* a_hand, float a_alpha, float a_weight, Pose::Arm& a_out)
	{
		auto up = a_root->GetObjectByName(a_upper), fore = a_root->GetObjectByName(a_fore), hand = a_root->GetObjectByName(a_hand);
		if (!up || !fore || !hand) return false;

		// Mario's own arm this frame, relative to him, blended between the last two ticks
		V3 t[3];
		for (int i = 0; i < 3; i++) {
			const V3 cur = Translation(s_cur[a_first + i]) - V3{ s_posCur[0], s_posCur[1], s_posCur[2] };
			const V3 prev = Translation(s_prev[a_first + i]) - V3{ s_posPrev[0], s_posPrev[1], s_posPrev[2] };
			t[i] = prev + (cur - prev) * a_alpha;
		}
		const V3    curU = t[1] - t[0], curF = t[2] - t[1];
		const float lenU = Len(curU), lenF = Len(curF);
		const V3    curUn = Norm(curU, V3{ 1, 0, 0 }), curFn = Norm(curF, V3{ 1, 0, 0 });

		// where Skyrim's arm points
		const V3 desU = Norm(ToSm64(fore->world.translate) - ToSm64(up->world.translate), curUn);
		const V3 desF = Norm(ToSm64(hand->world.translate) - ToSm64(fore->world.translate), curFn);

		const V3 uw = Norm(curUn + (desU - curUn) * a_weight, curUn);
		const M3 r1 = FromTo(curUn, uw);
		const V3 curF1 = Norm(Apply(curFn, r1), curFn);
		const V3 fw = Norm(curF1 + (desF - curF1) * a_weight, curF1);
		const M3 r2 = FromTo(curF1, fw);

		const V3 shoulder = t[0], elbow = shoulder + uw * lenU, wrist = elbow + fw * lenF;
		const V3 origin[3] = { shoulder, elbow, wrist };
		for (int i = 0; i < 3; i++) {
			M3 rot = Mul(Rotation(s_cur[a_first + i]), r1);
			if (i > 0) rot = Mul(rot, r2);
			float* m = a_out.m[i];
			for (int r = 0; r < 3; r++) {
				m[r * 4 + 0] = rot[r * 3 + 0];
				m[r * 4 + 1] = rot[r * 3 + 1];
				m[r * 4 + 2] = rot[r * 3 + 2];
				m[r * 4 + 3] = 0.0f;
			}
			m[12] = origin[i].x;
			m[13] = origin[i].y;
			m[14] = origin[i].z;
			m[15] = 1.0f;
		}
		a_out.handRel[0] = wrist.x;
		a_out.handRel[1] = wrist.y;
		a_out.handRel[2] = wrist.z;
		return true;
	}
}

void Pose::Reset()
{
	s_acting = false;
	s_tail = 0.0f;
	s_weight = s_wRight = s_wLeft = 0.0f;
	s_actRight = s_actLeft = false;
	s_tailRight = s_tailLeft = 0.0f;
	s_active = false;
	s_layoutOk = false;
	s_rightOk = s_leftOk = false;
}

void Pose::OnTick(const float a_marioPos[3], int a_partCount)
{
	Resolve();
	std::memcpy(s_prev, s_cur, sizeof(s_cur));
	std::memcpy(s_posPrev, s_posCur, sizeof(s_posCur));
	std::memcpy(s_posCur, a_marioPos, sizeof(s_posCur));
	s_layoutOk = s_resolved && a_partCount == 16;
	if (!s_layoutOk) return;
	for (int i = 0; i < 16; i++) std::memcpy(s_cur[i], s_partMatrix(i), sizeof(s_cur[i]));
	static bool s_first = true;
	if (s_first) {   // the very first tick has nothing before it
		std::memcpy(s_prev, s_cur, sizeof(s_cur));
		std::memcpy(s_posPrev, s_posCur, sizeof(s_posCur));
		s_first = false;
	}
}

void Pose::Track(RE::PlayerCharacter* a_player, float a_delta)
{
	bool right, left;
	ArmActions(a_player, right, left);
	// the animation runs a little past the game's own attack flag
	if (right) { s_actRight = true; s_tailRight = 0.3f; } else if ((s_tailRight -= a_delta) <= 0.0f) s_actRight = false;
	if (left) { s_actLeft = true; s_tailLeft = 0.3f; } else if ((s_tailLeft -= a_delta) <= 0.0f) s_actLeft = false;
	s_acting = s_actRight || s_actLeft;
	Ease(s_wRight, s_actRight, a_delta);
	Ease(s_wLeft, s_actLeft, a_delta);
	s_weight = std::max(s_wRight, s_wLeft);
}

void Pose::Compute(RE::PlayerCharacter* a_player, float a_alpha)
{
	s_active = false;
	s_rightOk = s_leftOk = false;
	if (!s_layoutOk || s_weight <= 0.01f) return;
	auto root = a_player->Get3D(false);
	if (!root) return;
	if (s_wRight > 0.01f) s_rightOk = ComputeArm(root, 7, "NPC R UpperArm [RUar]", "NPC R Forearm [RLar]", "NPC R Hand [RHnd]", a_alpha, s_wRight, s_right);
	if (s_wLeft > 0.01f) s_leftOk = ComputeArm(root, 4, "NPC L UpperArm [LUar]", "NPC L Forearm [LLar]", "NPC L Hand [LHnd]", a_alpha, s_wLeft, s_left);
	s_active = s_rightOk || s_leftOk;
	if (Config::telemetry) {
		static int s_log = 0;
		if (s_log++ % 20 == 0) logger::info("POSE weight={:.2f} right={} left={}", s_weight, s_rightOk, s_leftOk);
	}
}

bool        Pose::Active() { return s_acting; }
float       Pose::Weight() { return s_weight; }
float       Pose::WeightRight() { return s_wRight; }
float       Pose::WeightLeft() { return s_wLeft; }
const Pose::Arm* Pose::Right() { return s_rightOk ? &s_right : nullptr; }
const Pose::Arm* Pose::Left() { return s_leftOk ? &s_left : nullptr; }

bool Pose::Transform(int a_tri, int a_corner, float a_rel[3], float a_nrm[3])
{
	if (!s_active || !s_layoutOk) return false;
	const int part = s_triParts()[a_tri];
	const Arm* arm = nullptr;
	int        index = 0;
	if (part >= 7 && part <= 9 && s_rightOk) {
		arm = &s_right;
		index = part - 7;
	} else if (part >= 4 && part <= 6 && s_leftOk) {
		arm = &s_left;
		index = part - 4;
	}
	if (!arm) return false;
	const float* lp = s_localPos() + a_tri * 9 + a_corner * 3;
	const float* ln = s_localNrm() + a_tri * 9 + a_corner * 3;
	const float* m = arm->m[index];
	for (int axis = 0; axis < 3; axis++) {
		a_rel[axis] = lp[0] * m[axis] + lp[1] * m[4 + axis] + lp[2] * m[8 + axis] + m[12 + axis];
		a_nrm[axis] = ln[0] * m[axis] + ln[1] * m[4 + axis] + ln[2] * m[8 + axis];
	}
	return true;
}
