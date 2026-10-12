#include "Collision.h"
#include "Config.h"

#include <chrono>
#include <unordered_map>
#include <unordered_set>

namespace
{
	using namespace RE;

	struct V3
	{
		float x, y, z;
	};

	V3 Load(const hkVector4& a_v)
	{
		alignas(16) float f[4];
		_mm_store_ps(f, a_v.quad);
		return { f[0], f[1], f[2] };
	}

	// a Havok transform as plain floats: columns of the rotation, then the translation
	struct Xf
	{
		V3 c0{ 1, 0, 0 }, c1{ 0, 1, 0 }, c2{ 0, 0, 1 }, t{ 0, 0, 0 };

		static Xf From(const hkTransform& a_t)
		{
			return { Load(a_t.rotation.col0), Load(a_t.rotation.col1), Load(a_t.rotation.col2), Load(a_t.translation) };
		}
		V3 Apply(V3 p) const
		{
			return { c0.x * p.x + c1.x * p.y + c2.x * p.z + t.x,
				c0.y * p.x + c1.y * p.y + c2.y * p.z + t.y,
				c0.z * p.x + c1.z * p.y + c2.z * p.z + t.z };
		}
		Xf operator*(const Xf& b) const   // this after b
		{
			auto rot = [&](V3 v) { return V3{ c0.x * v.x + c1.x * v.y + c2.x * v.z, c0.y * v.x + c1.y * v.y + c2.y * v.z, c0.z * v.x + c1.z * v.y + c2.z * v.z }; };
			return { rot(b.c0), rot(b.c1), rot(b.c2), Apply(b.t) };
		}
	};

	template <class T>
	const T& At(const hkpShape* a_shape, std::size_t a_offset)
	{
		return *reinterpret_cast<const T*>(reinterpret_cast<const std::uint8_t*>(a_shape) + a_offset);
	}

	bool ValidShape(const hkpShape* a_shape)
	{
		return a_shape && static_cast<std::uint32_t>(a_shape->type) < static_cast<std::uint32_t>(hkpShapeType::kTotal);
	}

	// faces of a convex point set, outward wound (brute force: Skyrim's convex pieces are small)
	std::vector<std::array<int, 3>> Hull(const std::vector<V3>& p)
	{
		std::vector<std::array<int, 3>> faces;
		const int n = static_cast<int>(p.size());
		if (n < 4 || n > 96) return faces;
		V3 c{ 0, 0, 0 };
		for (auto& v : p) { c.x += v.x / n; c.y += v.y / n; c.z += v.z / n; }
		for (int i = 0; i < n; i++)
			for (int j = i + 1; j < n; j++)
				for (int k = j + 1; k < n; k++) {
					V3 a = p[i], b = p[j], d = p[k];
					V3 u{ b.x - a.x, b.y - a.y, b.z - a.z }, v{ d.x - a.x, d.y - a.y, d.z - a.z };
					V3 nrm{ u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
					float len = std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y + nrm.z * nrm.z);
					if (len < 1e-6f) continue;
					int pos = 0, neg = 0;
					for (int m = 0; m < n && !(pos && neg); m++) {
						float s = (nrm.x * (p[m].x - a.x) + nrm.y * (p[m].y - a.y) + nrm.z * (p[m].z - a.z)) / len;
						if (s > 1e-4f) pos++;
						else if (s < -1e-4f) neg++;
					}
					if (pos && neg) continue;
					// outward: the centroid is on the negative side
					float sc = nrm.x * (c.x - a.x) + nrm.y * (c.y - a.y) + nrm.z * (c.z - a.z);
					if (sc > 0) faces.push_back({ i, k, j });
					else faces.push_back({ i, j, k });
				}
		return faces;
	}

	struct Walker
	{
		V3                                       lo, hi;   // query box, Havok space
		float                                    toGame;   // Havok -> Skyrim units
		std::vector<Collision::Tri>*             out;
		Collision::Stats*                        stats;
		std::unordered_set<std::uint32_t>        warned;

		void Emit(V3 a, V3 b, V3 c)
		{
			float mnx = std::min({ a.x, b.x, c.x }), mxx = std::max({ a.x, b.x, c.x });
			float mny = std::min({ a.y, b.y, c.y }), mxy = std::max({ a.y, b.y, c.y });
			float mnz = std::min({ a.z, b.z, c.z }), mxz = std::max({ a.z, b.z, c.z });
			if (mxx < lo.x || mnx > hi.x || mxy < lo.y || mny > hi.y || mxz < lo.z || mnz > hi.z) return;
			auto g = [&](V3 v) { return NiPoint3{ v.x * toGame, v.y * toGame, v.z * toGame }; };
			out->push_back({ { g(a), g(b), g(c) } });
		}

		void Box(const Xf& x, V3 h)
		{
			V3 c[8];
			for (int i = 0; i < 8; i++) c[i] = x.Apply({ (i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z });
			static constexpr int f[12][3] = { { 0, 2, 3 }, { 0, 3, 1 }, { 4, 5, 7 }, { 4, 7, 6 }, { 0, 1, 5 }, { 0, 5, 4 },
				{ 2, 6, 7 }, { 2, 7, 3 }, { 0, 4, 6 }, { 0, 6, 2 }, { 1, 3, 7 }, { 1, 7, 5 } };
			for (auto& t : f) Emit(c[t[0]], c[t[1]], c[t[2]]);
		}

		void Walk(const hkpShape* s, const Xf& x, int depth)
		{
			if (!ValidShape(s) || depth > 8) return;
			switch (s->type) {
			case hkpShapeType::kTriangle:
				Emit(x.Apply(Load(At<hkVector4>(s, 0x30))), x.Apply(Load(At<hkVector4>(s, 0x40))), x.Apply(Load(At<hkVector4>(s, 0x50))));
				return;
			case hkpShapeType::kBox:
				Box(x, Load(At<hkVector4>(s, 0x30)));
				return;
			case hkpShapeType::kSphere:
				{
					float r = At<float>(s, 0x20);
					Box(x, { r, r, r });
					return;
				}
			case hkpShapeType::kCapsule:
				{
					// as the box around the capsule's segment (pillars, logs, branches)
					V3    a = Load(At<hkVector4>(s, 0x30)), b = Load(At<hkVector4>(s, 0x40));
					float r = At<float>(s, 0x20);
					Xf    xc = x;
					xc.t = x.Apply({ (a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2 });
					Box(xc, { std::abs(a.x - b.x) / 2 + r, std::abs(a.y - b.y) / 2 + r, std::abs(a.z - b.z) / 2 + r });
					return;
				}
			case hkpShapeType::kConvexVertices:
				{
					auto&           cv = *static_cast<const hkpConvexVerticesShape*>(s);
					std::vector<V3> pts;
					for (int i = 0; i < cv.numVertices; i++) {
						auto& q = cv.rotatedVertices[i / 4];
						alignas(16) float xs[4], ys[4], zs[4];
						_mm_store_ps(xs, q.x.quad);
						_mm_store_ps(ys, q.y.quad);
						_mm_store_ps(zs, q.z.quad);
						pts.push_back({ xs[i % 4], ys[i % 4], zs[i % 4] });
					}
					for (auto& f : Hull(pts)) Emit(x.Apply(pts[f[0]]), x.Apply(pts[f[1]]), x.Apply(pts[f[2]]));
					return;
				}
			case hkpShapeType::kConvexTranslate:
				{
					Xf t;
					t.t = Load(At<hkVector4>(s, 0x40));
					Walk(At<const hkpShape*>(s, 0x30), x * t, depth + 1);
					return;
				}
			case hkpShapeType::kConvexTransform:
				Walk(At<const hkpShape*>(s, 0x30), x * Xf::From(At<hkTransform>(s, 0x40)), depth + 1);
				return;
			case hkpShapeType::kTransform:
				Walk(At<const hkpShape*>(s, 0x28), x * Xf::From(At<hkTransform>(s, 0x50)), depth + 1);
				return;
			default:
				break;
			}
			// MOPP / compressed mesh / list / collections: Havok's own container interface
			if (auto container = s->GetContainer()) {
				hkpShapeBuffer buffer;
				for (auto key = container->GetFirstKey(); key != HK_INVALID_SHAPE_KEY; key = container->GetNextKey(key)) {
					Walk(container->GetChildShape(key, buffer), x, depth + 1);
				}
				return;
			}
			stats->unhandled++;
			if (warned.insert(static_cast<std::uint32_t>(s->type)).second) {
				logger::warn("collision: shape type {} not read", static_cast<std::uint32_t>(s->type));
			}
		}
	};

	// fixed bodies don't move: their world-space triangles are kept, checked against the body's AABB in case
	// the address is reused by another body after a cell unloads
	struct CacheEntry
	{
		const hkpShape*             shape;
		V3                          mn, mx;
		std::vector<Collision::Tri> tris;
	};
	std::unordered_map<const hkpEntity*, CacheEntry> s_cache;
	const hkpWorld*                                  s_cacheWorld = nullptr;

	bool Wanted(COL_LAYER a_layer)
	{
		switch (a_layer) {
		case COL_LAYER::kStatic:
		case COL_LAYER::kAnimStatic:
		case COL_LAYER::kTransparent:
		case COL_LAYER::kTrees:
		case COL_LAYER::kProps:
		case COL_LAYER::kTerrain:
		case COL_LAYER::kGround:
		case COL_LAYER::kInvisibleWall:
			return true;
		default:
			return false;
		}
	}
}

namespace
{
	void VisitCollisionObjects(RE::NiAVObject* a_obj, const std::function<void(RE::NiAVObject*, RE::NiCollisionObject*)>& a_fn)
	{
		if (!a_obj) return;
		if (auto col = a_obj->GetCollisionObject()) a_fn(a_obj, col);
		if (auto node = a_obj->AsNode())
			for (auto& child : node->GetChildren()) VisitCollisionObjects(child.get(), a_fn);
	}

	RE::hkpRigidBody* BodyOf(RE::NiCollisionObject* a_col)
	{
		auto bhk = skyrim_cast<RE::bhkCollisionObject*>(a_col);
		auto body = bhk ? bhk->GetRigidBody() : nullptr;
		return body ? static_cast<RE::hkpRigidBody*>(body->referencedObject.get()) : nullptr;
	}

	// every loaded door's rigid bodies within the radius of the player
	void ForEachDoorBody(RE::PlayerCharacter* a_player, float a_radius, const std::function<void(const RE::hkpRigidBody*)>& a_fn)
	{
		auto tes = RE::TES::GetSingleton();
		if (!tes) return;
		tes->ForEachReferenceInRange(a_player, a_radius, [&](RE::TESObjectREFR* ref) {
			auto base = ref->GetBaseObject();
			if (!base || !base->Is(RE::FormType::Door)) return RE::BSContainer::ForEachResult::kContinue;
			VisitCollisionObjects(ref->Get3D(), [&](RE::NiAVObject*, RE::NiCollisionObject* col) {
				if (auto body = BodyOf(col)) a_fn(body);
			});
			return RE::BSContainer::ForEachResult::kContinue;
		});
	}
}

namespace Collision
{
	Stats Gather(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_center, float a_radius, std::vector<Tri>& a_out)
	{
		Stats stats;
		auto  start = std::chrono::steady_clock::now();
		auto  bhk = a_cell ? a_cell->GetbhkWorld() : nullptr;
		auto  world = bhk ? bhk->GetWorld1() : nullptr;
		if (!world) return stats;

		const float toHavok = RE::bhkWorld::GetWorldScale();
		Walker      w;
		// bodies are read whole (cached), then their triangles culled to the query box
		w.lo = { -1e30f, -1e30f, -1e30f };
		w.hi = { 1e30f, 1e30f, 1e30f };
		const V3 qlo{ (a_center.x - a_radius) * toHavok, (a_center.y - a_radius) * toHavok, (a_center.z - a_radius) * toHavok };
		const V3 qhi{ (a_center.x + a_radius) * toHavok, (a_center.y + a_radius) * toHavok, (a_center.z + a_radius) * toHavok };
		const NiPoint3 glo{ a_center.x - a_radius, a_center.y - a_radius, a_center.z - a_radius };
		const NiPoint3 ghi{ a_center.x + a_radius, a_center.y + a_radius, a_center.z + a_radius };
		w.toGame = 1.0f / toHavok;
		w.stats = &stats;

		RE::BSReadLockGuard lock(bhk->worldLock);
		if (s_cacheWorld != world || s_cache.size() > 4000) {
			s_cache.clear();
			s_cacheWorld = world;
		}
		auto consider = [&](const hkpEntity* entity) {
			if (!entity) return;
			auto& collidable = entity->collidable;
			if (!Wanted(collidable.GetCollisionLayer())) return;
			auto shape = collidable.shape;
			if (!ValidShape(shape)) return;

			const auto& t = entity->motion.motionState.transform;
			RE::hkAabb  box;
			shape->GetAabbImpl(t, 0.0f, box);
			V3 mn = Load(box.min), mx = Load(box.max);
			if (mx.x < qlo.x || mn.x > qhi.x || mx.y < qlo.y || mn.y > qhi.y || mx.z < qlo.z || mn.z > qhi.z) return;

			stats.bodies++;
			auto& e = s_cache[entity];
			auto  same = [](V3 a, V3 b) { return std::abs(a.x - b.x) < 1e-3f && std::abs(a.y - b.y) < 1e-3f && std::abs(a.z - b.z) < 1e-3f; };
			if (e.shape != shape || !same(e.mn, mn) || !same(e.mx, mx)) {
				e.shape = shape;
				e.mn = mn;
				e.mx = mx;
				e.tris.clear();
				w.out = &e.tris;
				w.Walk(shape, Xf::From(t), 0);
			}
			for (auto& tri : e.tris) {
				auto& v = tri.v;
				if (std::max({ v[0].x, v[1].x, v[2].x }) < glo.x || std::min({ v[0].x, v[1].x, v[2].x }) > ghi.x ||
					std::max({ v[0].y, v[1].y, v[2].y }) < glo.y || std::min({ v[0].y, v[1].y, v[2].y }) > ghi.y ||
					std::max({ v[0].z, v[1].z, v[2].z }) < glo.z || std::min({ v[0].z, v[1].z, v[2].z }) > ghi.z)
					continue;
				a_out.push_back(tri);
			}
		};
		auto island = world->fixedIsland;
		if (!island) return stats;
		for (auto entity : island->entities) consider(entity);
		// doors are keyframed bodies, which the fixed island doesn't list: found through the door references, so a
		// closed door blocks Mario and an open one lets him through
		if (auto player = RE::PlayerCharacter::GetSingleton()) {
			ForEachDoorBody(player, a_radius + 500.0f, [&](const hkpRigidBody* body) {
				const auto before = stats.bodies;
				consider(body);
				stats.doors += stats.bodies - before;
			});
		}
		stats.tris = a_out.size();
		stats.ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
		return stats;
	}
}

namespace Collision
{
	std::uint64_t DoorSignature(RE::PlayerCharacter* a_player, float a_radius)
	{
		std::uint64_t h = 1469598103934665603ull;
		ForEachDoorBody(a_player, a_radius, [&](const RE::hkpRigidBody* body) {
			const auto& t = body->motion.motionState.transform;
			alignas(16) float f[4];
			_mm_store_ps(f, t.rotation.col0.quad);
			for (int i = 0; i < 3; i++) h = (h ^ static_cast<std::uint64_t>(std::lround(f[i] * 200.0f) + 1000)) * 1099511628211ull;
			_mm_store_ps(f, t.translation.quad);
			for (int i = 0; i < 3; i++) h = (h ^ static_cast<std::uint64_t>(std::lround(f[i] * 200.0f) + 100000)) * 1099511628211ull;
		});
		return h;
	}

	void LogDoorBodies(RE::PlayerCharacter* a_player, float a_radius)
	{
		const float toGame = 1.0f / RE::bhkWorld::GetWorldScale();
		ForEachDoorBody(a_player, a_radius, [&](const RE::hkpRigidBody* body) {
			const auto& t = body->motion.motionState.transform;
			RE::hkAabb  box;
			body->collidable.shape->GetAabbImpl(t, 0.0f, box);
			V3 mn = Load(box.min), mx = Load(box.max), tr = Load(t.translation);
			logger::info("DOORBODY at=({:.0f},{:.0f},{:.0f}) box=({:.0f},{:.0f},{:.0f})..({:.0f},{:.0f},{:.0f}) layer={} shape={}",
				tr.x * toGame, tr.y * toGame, tr.z * toGame, mn.x * toGame, mn.y * toGame, mn.z * toGame, mx.x * toGame, mx.y * toGame, mx.z * toGame,
				static_cast<int>(body->collidable.GetCollisionLayer()), static_cast<int>(body->collidable.shape->type));
		});
	}
}
