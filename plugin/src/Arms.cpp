#include "Arms.h"
#include "Config.h"

namespace
{
	// Mario's parts in draw order (16 in his usual states): 6 = left hand, 9 = right hand (tools: dumped facing +Z,
	// his right is -X)
	constexpr int kParts = 16, kLeftHand = 6, kRightHand = 9, kLeftForearm = 5, kRightForearm = 8;

	int (*s_partCount)() = nullptr;
	const float* (*s_partMatrix)(int) = nullptr;

	Arms::Hand s_right, s_left;

	struct Held
	{
		RE::NiAVObject*             source = nullptr;   // the player's original (under WEAPON / SHIELD)
		RE::NiPointer<RE::NiAVObject> clone;
	};
	Held s_weapon, s_shield;
	bool s_wasActing = false;

	void Resolve()
	{
		if (s_partCount) return;
		HMODULE dll = ::GetModuleHandleW(L"sm64.dll");
		if (!dll) return;
		s_partCount = reinterpret_cast<int (*)()>(::GetProcAddress(dll, "tes64_part_count"));
		s_partMatrix = reinterpret_cast<const float* (*)(int)>(::GetProcAddress(dll, "tes64_part_matrix"));
	}

	Arms::Hand ReadPart(int a_part, const std::function<RE::NiPoint3(const float*)>& a_toSkyrim)
	{
		Arms::Hand h;
		const float* m = s_partMatrix ? s_partMatrix(a_part) : nullptr;
		if (!m) return h;
		// SM64 matrices are row-vector: rows 0-2 the part's axes, row 3 its origin
		h.pos = a_toSkyrim(&m[12]);
		auto axis = [&](int r) {
			RE::NiPoint3 a{ m[r * 4 + 0], -m[r * 4 + 2], m[r * 4 + 1] };   // SM64 (y up) -> Skyrim (z up)
			float        l = a.Length();
			return l > 1e-6f ? a / l : a;
		};
		auto x = axis(0), y = axis(1), z = axis(2);
		// columns are the axes
		h.rot.entry[0][0] = x.x; h.rot.entry[1][0] = x.y; h.rot.entry[2][0] = x.z;
		h.rot.entry[0][1] = y.x; h.rot.entry[1][1] = y.y; h.rot.entry[2][1] = y.z;
		h.rot.entry[0][2] = z.x; h.rot.entry[1][2] = z.y; h.rot.entry[2][2] = z.z;
		h.valid = true;
		return h;
	}

	RE::NiMatrix3 Euler(float a_x, float a_y, float a_z)
	{
		RE::NiMatrix3 m;
		m.SetEulerAnglesXYZ(a_x * RE::NI_PI / 180.0f, a_y * RE::NI_PI / 180.0f, a_z * RE::NI_PI / 180.0f);
		return m;
	}

	// the original is part of the hidden player body (culled), and the clone copies its flags
	void Uncull(RE::NiAVObject* a_obj)
	{
		if (!a_obj) return;
		a_obj->SetAppCulled(false);
		if (auto node = a_obj->AsNode())
			for (auto& c : node->GetChildren()) Uncull(c.get());
	}

	RE::NiAVObject* FirstChild(RE::NiAVObject* a_node)
	{
		auto node = a_node ? a_node->AsNode() : nullptr;
		if (!node) return nullptr;
		for (auto& c : node->GetChildren())
			if (c) return c.get();
		return nullptr;
	}

	void Follow(Held& a_held, RE::NiAVObject* a_source, RE::NiNode* a_marioRoot, const Arms::Hand& a_hand, const RE::NiPoint3& a_root)
	{
		if (a_source != a_held.source) {
			if (a_held.clone && a_held.clone->parent) a_held.clone->parent->DetachChild2(a_held.clone.get());
			a_held.clone.reset();
			a_held.source = a_source;
			if (a_source) {
				a_held.clone.reset(static_cast<RE::NiAVObject*>(a_source->Clone()));
				if (a_held.clone) {
					Uncull(a_held.clone.get());
					a_marioRoot->AttachChild(a_held.clone.get(), true);
					if (Config::telemetry) logger::info("arms: holding {}", a_source->name.c_str());
				}
			}
		}
		if (!a_held.clone || !a_hand.valid) return;
		// Mario's node has no rotation (Render.cpp): local = world - root
		a_held.clone->local.translate = a_hand.pos - a_root;
		a_held.clone->local.rotate = a_hand.grip * Euler(Config::weaponRot[0], Config::weaponRot[1], Config::weaponRot[2]);
		a_held.clone->local.scale = Config::weaponScale;
	}
}

namespace
{
	// how a fist holds a weapon (Skyrim weapon meshes: blade along +Y, edge along X, flat face Z): the blade sticks
	// out across the fist, pointing as far "up" as the forearm allows - forward with the arm hanging, up when it
	// punches - and the edge leads along the forearm
	RE::NiMatrix3 Grip(const RE::NiPoint3& a_forearm, float a_heading)
	{
		const RE::NiPoint3 up{ 0.0f, 0.0f, 1.0f }, fwd{ std::sin(a_heading), std::cos(a_heading), 0.0f };
		RE::NiPoint3 x = a_forearm;
		RE::NiPoint3 y = up - x * up.Dot(x);
		if (y.Length() < 0.3f) y = fwd - x * fwd.Dot(x);
		y.Unitize();
		RE::NiPoint3 z = x.Cross(y);
		RE::NiMatrix3 m;
		m.entry[0][0] = x.x; m.entry[1][0] = x.y; m.entry[2][0] = x.z;
		m.entry[0][1] = y.x; m.entry[1][1] = y.y; m.entry[2][1] = y.z;
		m.entry[0][2] = z.x; m.entry[1][2] = z.y; m.entry[2][2] = z.z;
		return m;
	}
}

void Arms::ReadHands(const std::function<RE::NiPoint3(const float*)>& a_toSkyrim, float a_heading)
{
	Resolve();
	const int n = s_partCount ? s_partCount() : 0;
	if (n != kParts) {   // an unusual state (holding something, a cap in hand): keep last tick's hands
		return;
	}
	s_right = ReadPart(kRightHand, a_toSkyrim);
	s_left = ReadPart(kLeftHand, a_toSkyrim);
	auto forearm = [&](int a_part, const Hand& a_hand) {
		auto f = a_hand.pos - ReadPart(a_part, a_toSkyrim).pos;
		return f.Length() > 1e-3f ? f / f.Length() : RE::NiPoint3{ 0, 0, -1 };
	};
	s_right.grip = Grip(forearm(kRightForearm, s_right), a_heading);
	s_left.grip = Grip(forearm(kLeftForearm, s_left), a_heading);
}

const Arms::Hand& Arms::RightHand() { return s_right; }
const Arms::Hand& Arms::LeftHand() { return s_left; }

void Arms::Update(RE::PlayerCharacter* a_player, RE::NiNode* a_marioRoot, const RE::NiPoint3& a_root)
{
	if (!a_marioRoot) return;
	auto root = a_player->Get3D(false);
	const bool drawn = a_player->AsActorState()->IsWeaponDrawn();
	RE::NiAVObject* weapon = nullptr;
	RE::NiAVObject* shield = nullptr;
	if (root && drawn) {
		weapon = FirstChild(root->GetObjectByName("WEAPON"));
		shield = FirstChild(root->GetObjectByName("SHIELD"));
	}
	Follow(s_weapon, weapon, a_marioRoot, s_right, a_root);
	Follow(s_shield, shield, a_marioRoot, s_left, a_root);

	// spell hand effects and projectile origins: the magic nodes onto Mario's hands
	if (root) {
		auto place = [&](const char* a_name, const Hand& a_hand) {
			auto node = root->GetObjectByName(a_name);
			if (!node || !node->parent || !a_hand.valid) return;
			const auto& pw = node->parent->world;
			const auto  inv = pw.rotate.Transpose();
			const float s = pw.scale != 0.0f ? pw.scale : 1.0f;
			node->local.translate = (inv * (a_hand.pos - pw.translate)) / s;
			RE::NiUpdateData ctx{};
			node->Update(ctx);
		};
		place("NPC R MagicNode [RMag]", s_right);
		place("NPC L MagicNode [LMag]", s_left);
	}
}

void Arms::Clear()
{
	for (auto* h : { &s_weapon, &s_shield }) {
		if (h->clone && h->clone->parent) h->clone->parent->DetachChild2(h->clone.get());
		h->clone.reset();
		h->source = nullptr;
	}
	s_wasActing = false;
}

bool Arms::SkyrimActionStarted(RE::PlayerCharacter* a_player)
{
	const bool acting = a_player->IsAttacking() || a_player->WhoIsCasting() != 0;
	const bool started = acting && !s_wasActing;
	s_wasActing = acting;
	return started;
}
