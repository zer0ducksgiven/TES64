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
	constexpr M3 kIdentity{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	M3 Mul(const M3& a, const M3& b)
	{
		M3 c{};
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				for (int k = 0; k < 3; k++) c[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
		return c;
	}
	M3 Transpose(const M3& a) { return { a[0], a[3], a[6], a[1], a[4], a[7], a[2], a[5], a[8] }; }
	V3 Apply(V3 v, const M3& m)
	{
		return { v.x * m[0] + v.y * m[3] + v.z * m[6], v.x * m[1] + v.y * m[4] + v.z * m[7], v.x * m[2] + v.y * m[5] + v.z * m[8] };
	}
	// the rotation that turns unit a onto unit b (a * R = b)
	M3 FromTo(V3 a, V3 b)
	{
		const V3    v = Cross(a, b);
		const float c = Dot(a, b);
		M3          rc = kIdentity;   // column-vector form first, transposed at the end
		if (c < -0.9999f) {           // opposite: half a turn about anything perpendicular
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
		return Transpose(rc);
	}
	// a rotation scaled toward none: its angle times a_t, about the same axis (row form in, row form out)
	M3 Partial(const M3& a_r, float a_t)
	{
		if (a_t >= 0.999f) return a_r;
		const float c = std::clamp((a_r[0] + a_r[4] + a_r[8] - 1.0f) / 2.0f, -1.0f, 1.0f);
		const float angle = std::acos(c);
		if (angle < 1e-4f) return kIdentity;
		const M3 col = Transpose(a_r);   // the axis from the column form's skew part
		V3       axis{ col[7] - col[5], col[2] - col[6], col[3] - col[1] };
		axis = Norm(axis, V3{ 0, 1, 0 });
		const float a = angle * a_t, s = std::sin(a), cc = std::cos(a), t = 1 - cc;
		const M3    out{ t * axis.x * axis.x + cc, t * axis.x * axis.y - s * axis.z, t * axis.x * axis.z + s * axis.y,
			   t * axis.x * axis.y + s * axis.z, t * axis.y * axis.y + cc, t * axis.y * axis.z - s * axis.x,
			   t * axis.x * axis.z - s * axis.y, t * axis.y * axis.z + s * axis.x, t * axis.z * axis.z + cc };
		return Transpose(out);
	}
	// about SM64's up axis: Mario's facing f turned to f + a
	V3 Yawed(V3 v, float a) { return { v.x * std::cos(a) + v.z * std::sin(a), v.y, -v.x * std::sin(a) + v.z * std::cos(a) }; }
	float Wrap(float a)
	{
		while (a > RE::NI_PI) a -= RE::NI_TWO_PI;
		while (a < -RE::NI_PI) a += RE::NI_TWO_PI;
		return a;
	}

	// ---- state ---------------------------------------------------------------------------------------------------
	const unsigned char* (*s_triParts)() = nullptr;
	const float* (*s_localPos)() = nullptr;
	const float* (*s_localNrm)() = nullptr;
	const float* (*s_partMatrix)(int) = nullptr;
	bool s_resolved = false;

	// libsm64's parts: 0 root, 1 pelvis, 2 torso, 3 head, 4-6 left arm, 7-9 right arm (upper, fore, hand), 10-15 legs
	constexpr int kParts = 16, kPelvis = 1, kTorso = 2, kHead = 3, kLeftArm = 4, kRightArm = 7, kLeftLeg = 10, kRightLeg = 13;
	float s_cur[kParts][16], s_prev[kParts][16];
	float s_posCur[3], s_posPrev[3];
	bool  s_layoutOk = false;   // Mario has his usual 16 parts this tick

	bool  s_acting = false;
	float s_weight = 0.0f;                  // the larger of the two arms'
	float s_wRight = 0.0f, s_wLeft = 0.0f;  // how much of Skyrim's pose each arm shows
	float s_tailRight = 0.0f, s_tailLeft = 0.0f;
	bool  s_actRight = false, s_actLeft = false;
	bool  s_active = false;     // the pose is showing this frame
	float s_yaw = 0.0f;         // Mario's body turned this much (SM64 radians) from where he runs, toward the crosshair

	// Skyrim's pelvis, spine and head in the actor's own frame while it stands idle: the pose is how far they move from that
	bool          s_haveRest = false;
	RE::NiMatrix3 s_restPelvis, s_restSpine, s_restHead;
	RE::NiPoint3  s_restPelvisOff;   // the pelvis's place from the root, in the actor's frame, standing
	// Skyrim's own full-body animation (furniture, a killmove): Mario's whole body is the player's, and he's where they are
	bool  s_mirror = false;      // Skyrim is animating the whole body now
	float s_mirrorW = 0.0f;      // how much of it shows (eased)
	int   s_forceMirror = -1;    // debug: -1 by the game, 0 off, 1 on
	Pose::Arm s_legL, s_legR;    // the legs' chains (their matrices aren't used: the parts are)
	constexpr float kMaxTwist = 1.75f;   // radians the upper body turns on the legs; past that the legs turn too

	// posed parts this frame (row-vector, relative to Mario, before the body yaw)
	struct Part
	{
		M3   rot = kIdentity;
		V3   t;
		bool posed = false;
	};
	Part      s_parts[kParts];
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
	M3 Rotation(const float (&a_m)[16]) { return { a_m[0], a_m[1], a_m[2], a_m[4], a_m[5], a_m[6], a_m[8], a_m[9], a_m[10] }; }
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
		// a weapon (or a spell) in the left hand too: dual wielding swings both
		auto       left = a_player->GetEquippedObject(true);
		const bool leftArmed = left && (left->Is(RE::FormType::Weapon) || left->Is(RE::FormType::Spell));
		a_right = a_player->IsAttacking() || casting || moving || blocking;
		a_left = casting || moving || blocking || (a_player->IsAttacking() && leftArmed);
	}

	void Ease(float& a_w, bool a_on, float a_dt)
	{
		const float target = a_on ? 1.0f : 0.0f;
		a_w += (target - a_w) * std::min(1.0f, a_dt * 14.0f);
		if (std::abs(target - a_w) < 0.01f) a_w = target;
	}

	// a rotation in the actor's own frame (column form: x right, y forward, z up) as a row-form rotation in Mario's
	// SM64 frame when he faces a_face
	M3 ActorToMario(const RE::NiMatrix3& a_d, float a_face)
	{
		const V3 axes[3] = { { -std::cos(a_face), 0, std::sin(a_face) }, { std::sin(a_face), 0, std::cos(a_face) }, { 0, 1, 0 } };
		M3       col{};   // M D M^T, M's columns the actor's axes in SM64
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++) {
				float v = 0;
				for (int a = 0; a < 3; a++)
					for (int b = 0; b < 3; b++) v += (&axes[a].x)[i] * a_d.entry[a][b] * (&axes[b].x)[j];
				col[i * 3 + j] = v;
			}
		return Transpose(col);
	}

	RE::NiMatrix3 Blend(const RE::NiMatrix3& a, const RE::NiMatrix3& b, float t)
	{
		RE::NiMatrix3 m;
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++) m.entry[i][j] = a.entry[i][j] + (b.entry[i][j] - a.entry[i][j]) * t;
		RE::NiPoint3 c0{ m.entry[0][0], m.entry[1][0], m.entry[2][0] }, c1{ m.entry[0][1], m.entry[1][1], m.entry[2][1] };
		c0.Unitize();
		c1 = c1 - c0 * c1.Dot(c0);
		c1.Unitize();
		const RE::NiPoint3 c[3] = { c0, c1, c0.Cross(c1) };
		for (int k = 0; k < 3; k++) { m.entry[0][k] = c[k].x; m.entry[1][k] = c[k].y; m.entry[2][k] = c[k].z; }
		return m;
	}

	void Store(const Part& a_p, float* a_m)
	{
		for (int r = 0; r < 3; r++) {
			a_m[r * 4 + 0] = a_p.rot[r * 3 + 0];
			a_m[r * 4 + 1] = a_p.rot[r * 3 + 1];
			a_m[r * 4 + 2] = a_p.rot[r * 3 + 2];
			a_m[r * 4 + 3] = 0.0f;
		}
		a_m[12] = a_p.t.x;
		a_m[13] = a_p.t.y;
		a_m[14] = a_p.t.z;
		a_m[15] = 1.0f;
	}

	// hands are handed out after the body yaw (Arms puts the weapon there)
	void Hand(int a_first, Pose::Arm& a_out)
	{
		const V3 hw = Yawed(s_parts[a_first + 2].t, s_yaw);
		a_out.handRel[0] = hw.x;
		a_out.handRel[1] = hw.y;
		a_out.handRel[2] = hw.z;
		for (int i = 0; i < 3; i++) Store(s_parts[a_first + i], a_out.m[i]);
	}

	// one arm: its three parts (shoulder, forearm, hand) at a_first.., already carried by the torso, turned so each
	// segment points where Skyrim's skeleton's does
	bool ComputeArm(RE::NiAVObject* a_root, int a_first, const char* a_upper, const char* a_fore, const char* a_hand, float a_weight, Pose::Arm& a_out)
	{
		auto up = a_root->GetObjectByName(a_upper), fore = a_root->GetObjectByName(a_fore), hand = a_root->GetObjectByName(a_hand);
		if (!up || !fore || !hand) return false;

		V3 t[3];
		for (int i = 0; i < 3; i++) t[i] = s_parts[a_first + i].t;
		const V3    curU = t[1] - t[0], curF = t[2] - t[1];
		const float lenU = Len(curU), lenF = Len(curF);
		const V3    curUn = Norm(curU, V3{ 1, 0, 0 }), curFn = Norm(curF, V3{ 1, 0, 0 });

		// where Skyrim's arm points, in Mario's frame before his body is turned to the crosshair
		const V3 desU = Norm(Yawed(ToSm64(fore->world.translate) - ToSm64(up->world.translate), -s_yaw), curUn);
		const V3 desF = Norm(Yawed(ToSm64(hand->world.translate) - ToSm64(fore->world.translate), -s_yaw), curFn);

		const V3 uw = Norm(curUn + (desU - curUn) * a_weight, curUn);
		const M3 r1 = FromTo(curUn, uw);
		const V3 curF1 = Norm(Apply(curFn, r1), curFn);
		const V3 fw = Norm(curF1 + (desF - curF1) * a_weight, curF1);
		const M3 r2 = FromTo(curF1, fw);

		const V3 origin[3] = { t[0], t[0] + uw * lenU, t[0] + uw * lenU + fw * lenF };
		for (int i = 0; i < 3; i++) {
			auto& p = s_parts[a_first + i];
			p.rot = Mul(p.rot, r1);
			if (i > 0) p.rot = Mul(p.rot, r2);
			p.t = origin[i];
			p.posed = true;
		}
		Hand(a_first, a_out);
		return true;
	}
}

void Pose::Reset()
{
	s_acting = false;
	s_weight = s_wRight = s_wLeft = 0.0f;
	s_actRight = s_actLeft = false;
	s_tailRight = s_tailLeft = 0.0f;
	s_active = false;
	s_yaw = 0.0f;
	s_layoutOk = false;
	s_rightOk = s_leftOk = false;
}

void Pose::OnTick(const float a_marioPos[3], int a_partCount)
{
	Resolve();
	std::memcpy(s_prev, s_cur, sizeof(s_cur));
	std::memcpy(s_posPrev, s_posCur, sizeof(s_posCur));
	std::memcpy(s_posCur, a_marioPos, sizeof(s_posCur));
	s_layoutOk = s_resolved && a_partCount == kParts;
	if (!s_layoutOk) return;
	for (int i = 0; i < kParts; i++) std::memcpy(s_cur[i], s_partMatrix(i), sizeof(s_cur[i]));
	static bool s_first = true;
	if (s_first) {   // the very first tick has nothing before it
		std::memcpy(s_prev, s_cur, sizeof(s_cur));
		std::memcpy(s_posPrev, s_posCur, sizeof(s_posCur));
		s_first = false;
	}
}

void Pose::Track(RE::PlayerCharacter* a_player, float a_delta)
{
	// Skyrim's own full-body animations: sitting, sleeping, using furniture (a chair, a bed, a workbench), and killmoves
	// (its executions)
	const auto state = a_player->AsActorState()->GetSitSleepState();
	const bool furniture = a_player->GetOccupiedFurniture().get() != nullptr || state == RE::SIT_SLEEP_STATE::kIsSitting ||
	                       state == RE::SIT_SLEEP_STATE::kIsSleeping;
	const bool killmove = a_player->IsInKillMove();
	s_mirror = s_forceMirror >= 0 ? s_forceMirror == 1 : (furniture || killmove);
	Ease(s_mirrorW, s_mirror, a_delta);

	bool right, left;
	ArmActions(a_player, right, left);
	if (s_mirror) right = left = true;   // the arms are the player's too
	// the animation runs a little past the game's own attack flag
	if (right) { s_actRight = true; s_tailRight = 0.3f; } else if ((s_tailRight -= a_delta) <= 0.0f) s_actRight = false;
	if (left) { s_actLeft = true; s_tailLeft = 0.3f; } else if ((s_tailLeft -= a_delta) <= 0.0f) s_actLeft = false;
	s_acting = s_actRight || s_actLeft;
	Ease(s_wRight, s_actRight, a_delta);
	Ease(s_wLeft, s_actLeft, a_delta);
	s_weight = std::max(s_wRight, s_wLeft);
}

void Pose::Compute(RE::PlayerCharacter* a_player, float a_alpha, float a_face)
{
	s_active = false;
	s_rightOk = s_leftOk = false;
	s_yaw = 0.0f;
	auto root = a_player->Get3D(false);
	if (!root) return;
	auto spine = root->GetObjectByName("NPC Spine2 [Spn2]"), head = root->GetObjectByName("NPC Head [Head]"), pelvis = root->GetObjectByName("NPC Pelvis [Pelv]");
	const RE::NiMatrix3 actorT = root->world.rotate.Transpose();
	const RE::NiPoint3  rootAt = root->world.translate;

	// idle: learn how Skyrim's pelvis, spine and head sit in the actor's frame
	if (s_weight <= 0.0f && spine && head && pelvis) {
		const auto         pv = actorT * pelvis->world.rotate, sp = actorT * spine->world.rotate, hd = actorT * head->world.rotate;
		const RE::NiPoint3 off = actorT * (pelvis->world.translate - rootAt);
		s_restPelvis = s_haveRest ? Blend(s_restPelvis, pv, 0.1f) : pv;
		s_restSpine = s_haveRest ? Blend(s_restSpine, sp, 0.1f) : sp;
		s_restHead = s_haveRest ? Blend(s_restHead, hd, 0.1f) : hd;
		s_restPelvisOff = s_haveRest ? RE::NiPoint3{ s_restPelvisOff.x + (off.x - s_restPelvisOff.x) * 0.1f,
		                                  s_restPelvisOff.y + (off.y - s_restPelvisOff.y) * 0.1f,
		                                  s_restPelvisOff.z + (off.z - s_restPelvisOff.z) * 0.1f }
		                             : off;
		s_haveRest = true;
	}
	if (!s_layoutOk || s_weight <= 0.01f) return;

	// Skyrim's full-body animation: the body is the player's, facing the player's way (Mario faces that too, Mario.cpp)
	const bool mirror = s_mirrorW > 0.0f;
	// where he looks: the crosshair, against where SM64 runs him (the stick)
	const float toCrosshair = mirror ? 0.0f : Wrap(RE::NI_PI - a_player->GetAngleZ() - a_face);
	// Skyrim's hips turning in the swing (a spin attack goes all the way round): his legs turn with them
	float hips = 0.0f;
	RE::NiMatrix3 unturn;   // the hips' turn taken back out of the spine's, which rides on them
	unturn.entry[0][0] = unturn.entry[1][1] = unturn.entry[2][2] = 1.0f;
	if (!mirror && Config::bodyPose && s_haveRest && pelvis) {
		const auto d = (actorT * pelvis->world.rotate) * s_restPelvis.Transpose();
		hips = std::atan2(d.entry[1][0], d.entry[0][0]);   // about the actor's up, counter-clockwise
		const float c = std::cos(-hips), s = std::sin(-hips);
		unturn.entry[0][0] = c; unturn.entry[0][1] = -s;
		unturn.entry[1][0] = s; unturn.entry[1][1] = c;
	}
	// the upper body twists toward the crosshair on legs that keep running where he goes, as far as a body can; more
	// than that and the legs come round too. Without the body pose, all of him turns.
	float twist = (Config::bodyPose && !mirror) ? std::clamp(toCrosshair, -kMaxTwist, kMaxTwist) : 0.0f;
	s_yaw = (toCrosshair - twist + hips) * s_weight;
	twist *= s_weight;

	// Mario's parts this frame, relative to him, blended between the last two ticks
	for (int i = 0; i < kParts; i++) {
		const V3 cur = Translation(s_cur[i]) - V3{ s_posCur[0], s_posCur[1], s_posCur[2] };
		const V3 prev = Translation(s_prev[i]) - V3{ s_posPrev[0], s_posPrev[1], s_posPrev[2] };
		s_parts[i] = { Rotation(s_cur[i]), prev + (cur - prev) * a_alpha, false };
	}

	// furniture and killmoves: the pelvis and the legs go where Skyrim's pelvis is (a seat lowers it, a bed tilts it), and
	// the upper body moves with it (translation only: its own turn comes below)
	if (mirror && s_haveRest && pelvis) {
		const auto         pd = (actorT * pelvis->world.rotate) * s_restPelvis.Transpose();
		const M3           dPel = Partial(ActorToMario(pd, a_face), s_mirrorW);
		const RE::NiPoint3 dOff = actorT * (pelvis->world.translate - rootAt) - s_restPelvisOff;   // Skyrim units, actor's frame
		const float        cy = std::cos(a_face), sy = std::sin(a_face);
		const V3           ax[3] = { { -cy, 0, sy }, { sy, 0, cy }, { 0, 1, 0 } };   // the actor's axes in SM64
		const V3           dT = (ax[0] * dOff.x + ax[1] * dOff.y + ax[2] * dOff.z) * (s_mirrorW / Config::scale);
		const V3           pv = s_parts[kPelvis].t;
		for (int i = kPelvis; i < kParts; i++) {
			auto& p = s_parts[i];
			p.t = pv + Apply(p.t - pv, dPel) + dT;
			if (i == kPelvis || i >= kLeftLeg) {
				p.rot = Mul(p.rot, dPel);
				p.posed = true;
			}
		}
	}

	// torso and head: Skyrim's spine lean and twist (and the head's turn) on Mario's upper body, pivoting at his waist;
	// his arms ride along before they're aimed, so a swing winds up with the body instead of through it
	if (Config::bodyPose && s_haveRest && spine && head) {
		const auto sp = actorT * spine->world.rotate, hd = actorT * head->world.rotate;
		// the spine's and head's own movement (without the hips' turn, which the whole body already has), then the
		// twist to the crosshair, about the vertical
		const float tc = std::cos(twist), ts = std::sin(twist);
		const M3    yTwist{ tc, 0, -ts, 0, 1, 0, ts, 0, tc };
		const M3    dSpine = Mul(Partial(ActorToMario(unturn * (sp * s_restSpine.Transpose()), a_face), s_weight), yTwist);
		const M3    dHead = Mul(Partial(ActorToMario(unturn * (hd * s_restHead.Transpose()), a_face), s_weight), yTwist);
		const V3    pivot = s_parts[kTorso].t;
		for (int i = kTorso; i <= kRightArm + 2; i++) {
			auto& p = s_parts[i];
			p.t = pivot + Apply(p.t - pivot, dSpine);
			p.rot = Mul(p.rot, i == kHead ? dHead : dSpine);
			p.posed = true;
		}
	}

	// the legs in furniture and killmoves: their bones' directions, as the arms' are
	if (mirror && s_haveRest && pelvis) {
		ComputeArm(root, kLeftLeg, "NPC L Thigh [LThg]", "NPC L Calf [LClf]", "NPC L Foot [Lft ]", s_mirrorW, s_legL);
		ComputeArm(root, kRightLeg, "NPC R Thigh [RThg]", "NPC R Calf [RClf]", "NPC R Foot [Rft ]", s_mirrorW, s_legR);
	}

	if (s_wRight > 0.01f) s_rightOk = ComputeArm(root, kRightArm, "NPC R UpperArm [RUar]", "NPC R Forearm [RLar]", "NPC R Hand [RHnd]", s_wRight, s_right);
	if (s_wLeft > 0.01f) s_leftOk = ComputeArm(root, kLeftArm, "NPC L UpperArm [LUar]", "NPC L Forearm [LLar]", "NPC L Hand [LHnd]", s_wLeft, s_left);
	// an arm Skyrim isn't using still rides on the torso
	if (!s_rightOk && s_parts[kRightArm].posed) { Hand(kRightArm, s_right); s_rightOk = true; }
	if (!s_leftOk && s_parts[kLeftArm].posed) { Hand(kLeftArm, s_left); s_leftOk = true; }
	s_active = true;
	if (Config::telemetry) {
		static int s_log = 0;
		if (s_log++ % 20 == 0) logger::info("POSE weight={:.2f} right={} left={} yaw={:.2f} mirror={:.2f}", s_weight, s_rightOk, s_leftOk, s_yaw, s_mirrorW);
	}
}

bool Pose::Mirroring() { return s_mirror || s_mirrorW > 0.01f; }

void Pose::ForceMirror(int a_mode) { s_forceMirror = a_mode; }

bool             Pose::Active() { return s_acting; }
float            Pose::Weight() { return s_weight; }
float            Pose::WeightRight() { return s_wRight; }
float            Pose::WeightLeft() { return s_wLeft; }
float            Pose::Yaw() { return s_yaw; }
const Pose::Arm* Pose::Right() { return s_rightOk ? &s_right : nullptr; }
const Pose::Arm* Pose::Left() { return s_leftOk ? &s_left : nullptr; }

bool Pose::Transform(int a_tri, int a_corner, float a_rel[3], float a_nrm[3])
{
	if (!s_active || !s_layoutOk) return false;
	const int part = s_triParts()[a_tri];
	if (part < 0 || part >= kParts || !s_parts[part].posed) return false;
	const auto&  p = s_parts[part];
	const float* lp = s_localPos() + a_tri * 9 + a_corner * 3;
	const float* ln = s_localNrm() + a_tri * 9 + a_corner * 3;
	const V3     pos = Apply({ lp[0], lp[1], lp[2] }, p.rot) + p.t;
	const V3     nrm = Apply({ ln[0], ln[1], ln[2] }, p.rot);
	a_rel[0] = pos.x; a_rel[1] = pos.y; a_rel[2] = pos.z;
	a_nrm[0] = nrm.x; a_nrm[1] = nrm.y; a_nrm[2] = nrm.z;
	return true;   // before the body yaw: Render turns every vertex by Yaw()
}
