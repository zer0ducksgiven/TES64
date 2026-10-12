#include "Render.h"
#include "Config.h"

#include <DirectXPackedVector.h>
#include <d3d11.h>
#include <fstream>

namespace
{
	constexpr std::uint32_t kMaxTris = SM64_GEO_MAX_TRIANGLES;
	constexpr std::uint32_t kVerts = kMaxTris * 3;
	constexpr float         kBodyBrighten = 1.25f;   // libsm64's colours carry SM64's own shading; Skyrim lights them again

	// the template's vertex format (vanilla clutter's 0x3B: position+bitangentX, uv, normal, tangent, colour)
	struct Vertex
	{
		float         pos[3];
		float         bitangentX;
		std::uint16_t uv[2];
		std::uint8_t  normal[3];
		std::uint8_t  bitangentY;
		std::uint8_t  tangent[3];
		std::uint8_t  bitangentZ;
		std::uint8_t  color[4];
	};
	static_assert(sizeof(Vertex) == 32);

	struct Shape
	{
		RE::BSGeometry* geometry = nullptr;
		ID3D11Buffer*   buffer = nullptr;
		std::uint8_t*   raw = nullptr;   // the renderer's CPU copy
		std::vector<Vertex> verts;
	};

	RE::NiPointer<RE::NiNode> s_root;
	Shape                     s_body, s_decals;
	bool                      s_attached = false;
	std::uint32_t             s_drawn = 0;

	std::uint8_t Unorm(float f) { return static_cast<std::uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); }
	std::uint8_t Snorm(float f) { return static_cast<std::uint8_t>(std::clamp(f * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f); }

	bool Prepare(Shape& a_shape, const char* a_name)
	{
		auto obj = s_root->GetObjectByName(a_name);
		a_shape.geometry = obj ? obj->AsGeometry() : nullptr;
		if (!a_shape.geometry) {
			logger::error("render: {} missing from mario.nif", a_name);
			return false;
		}
		auto data = a_shape.geometry->GetGeometryRuntimeData().rendererData;
		if (!data) {
			logger::error("render: {} has no renderer data", a_name);
			return false;
		}
		// the template's own GPU buffer (default usage) is rewritten in place every frame
		a_shape.buffer = reinterpret_cast<ID3D11Buffer*>(data->vertexBuffer);
		a_shape.raw = data->rawVertexData;
		if (!a_shape.buffer) {
			logger::error("render: {} has no vertex buffer", a_name);
			return false;
		}
		a_shape.verts.assign(kVerts, Vertex{});
		return true;
	}

	void Upload(Shape& a_shape)
	{
		auto context = reinterpret_cast<ID3D11DeviceContext*>(RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context);
		if (context) context->UpdateSubresource(a_shape.buffer, 0, nullptr, a_shape.verts.data(), 0, 0);
		if (a_shape.raw) std::memcpy(a_shape.raw, a_shape.verts.data(), sizeof(Vertex) * kVerts);
	}
}

bool Render::WriteAtlas()
{
	constexpr std::uint32_t w = SM64_TEXTURE_WIDTH, h = SM64_TEXTURE_HEIGHT;
	if (Sm64::texture.size() != 4ull * w * h) return false;
	std::filesystem::create_directories("Data/Textures/TES64");
	std::ofstream f("Data/Textures/TES64/mario.dds", std::ios::binary);
	if (!f) {
		logger::error("render: can't write Data/Textures/TES64/mario.dds");
		return false;
	}
	std::uint32_t hdr[32] = { 0x20534444, 124, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000, h, w, w * 4, 0, 1 };
	hdr[19] = 32;           // pixel format size
	hdr[20] = 0x41;         // RGB | alpha
	hdr[22] = 32;
	hdr[23] = 0x00FF0000; hdr[24] = 0x0000FF00; hdr[25] = 0x000000FF; hdr[26] = 0xFF000000;
	hdr[27] = 0x1000;       // texture
	f.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
	std::vector<std::uint8_t> bgra(Sm64::texture.size());
	for (std::size_t i = 0; i < bgra.size(); i += 4) {
		bgra[i] = Sm64::texture[i + 2];
		bgra[i + 1] = Sm64::texture[i + 1];
		bgra[i + 2] = Sm64::texture[i];
		bgra[i + 3] = Sm64::texture[i + 3];
	}
	f.write(reinterpret_cast<const char*>(bgra.data()), static_cast<std::streamsize>(bgra.size()));
	return true;
}

bool Render::Show()
{
	if (!s_root) {
		RE::BSModelDB::DBTraits::ArgsType args{};
		if (RE::BSModelDB::Demand("TES64\\mario.nif", s_root, args) != RE::BSResource::ErrorCode::kNone || !s_root) {
			logger::error("render: couldn't load meshes/TES64/mario.nif");
			return false;
		}
		if (!Prepare(s_body, "MarioBody") || !Prepare(s_decals, "MarioDecals")) {
			s_root.reset();
			return false;
		}
		logger::info("render: mario.nif ready");
	}
	if (!s_attached) {
		// under the player's own root (drawn for sure); the player's other children get culled (Mario.cpp)
		auto player = RE::PlayerCharacter::GetSingleton();
		auto root3D = player ? player->Get3D(false) : nullptr;
		auto parent = root3D ? root3D->AsNode() : nullptr;
		if (!parent) {
			logger::error("render: the player has no 3D to attach Mario to");
			return false;
		}
		parent->AttachChild(s_root.get(), true);
		s_attached = true;
	}
	return true;
}

void Render::Hide()
{
	if (s_attached && s_root && s_root->parent) {
		s_root->parent->DetachChild2(s_root.get());
	}
	s_attached = false;
	s_drawn = 0;
}

bool Render::Visible() { return s_attached; }
RE::NiAVObject* Render::Root() { return s_root.get(); }
std::uint32_t Render::TrianglesDrawn() { return s_drawn; }

void Render::Update(const SM64MarioGeometryBuffers& a_geo, const float* a_prevPos, std::uint32_t a_prevTris,
	const float a_sm64Pos[3], const float a_sm64Prev[3], float a_alpha, const RE::NiPoint3& a_root, float a_scale, std::uint32_t a_flags)
{
	const bool metal = a_flags & 0x04, vanish = a_flags & 0x02;
	if (!s_attached || !s_root) return;
	using DirectX::PackedVector::XMConvertFloatToHalf;

	const std::uint32_t n = std::min<std::uint32_t>(a_geo.numTrianglesUsed, kMaxTris);
	std::uint32_t       decals = 0;
	for (std::uint32_t t = 0; t < kMaxTris; t++) {
		const bool used = t < n;
		// libsm64 marks untextured triangles with uv (1,1)
		const bool textured = used && !(a_geo.uv[t * 6] == 1.0f && a_geo.uv[t * 6 + 1] == 1.0f);
		if (textured) decals++;
		for (int k = 0; k < 3; k++) {
			const std::uint32_t i = t * 3 + k;
			Vertex&             b = s_body.verts[i];
			Vertex&             d = s_decals.verts[i];
			if (!used) {
				b = Vertex{};
				d = Vertex{};
				continue;
			}
			// SM64 offset from Mario, blended between the last two ticks, then Skyrim's axes (y up -> z up, north = -z)
			float rel[3];
			for (int axis = 0; axis < 3; axis++) {
				const float cur = a_geo.position[i * 3 + axis] - a_sm64Pos[axis];
				rel[axis] = t < a_prevTris ? std::lerp(a_prevPos[i * 3 + axis] - a_sm64Prev[axis], cur, a_alpha) : cur;
			}
			const RE::NiPoint3 p{ rel[0] * a_scale, -rel[2] * a_scale, rel[1] * a_scale };
			const float* nr = &a_geo.normal[i * 3];
			const float  nx = nr[0], ny = -nr[2], nz = nr[1];   // SM64 (y up) -> Skyrim (z up)
			const float* c = &a_geo.color[i * 3];

			b.pos[0] = p.x; b.pos[1] = p.y; b.pos[2] = p.z; b.bitangentX = 0.0f;
			b.uv[0] = XMConvertFloatToHalf(0.5f); b.uv[1] = XMConvertFloatToHalf(0.5f);
			b.normal[0] = Snorm(nx); b.normal[1] = Snorm(ny); b.normal[2] = Snorm(nz); b.bitangentY = 128;
			// any tangent perpendicular-ish to the normal: the normal map is flat
			const bool  up = std::abs(nz) > 0.9f;
			b.tangent[0] = Snorm(up ? 1.0f : -ny); b.tangent[1] = Snorm(up ? 0.0f : nx); b.tangent[2] = Snorm(0.0f); b.bitangentZ = 128;
			b.color[0] = Unorm(c[0] * kBodyBrighten); b.color[1] = Unorm(c[1] * kBodyBrighten); b.color[2] = Unorm(c[2] * kBodyBrighten); b.color[3] = 255;
			if (metal) {   // Metal Mario: polished silver, shaded by the light his SM64 colour carries
				const float l = std::clamp(0.35f + 0.65f * (c[0] + c[1] + c[2]) / 3.0f * kBodyBrighten, 0.0f, 1.0f);
				b.color[0] = Unorm(l * 0.85f); b.color[1] = Unorm(l * 0.88f); b.color[2] = Unorm(l * 0.95f);
			} else if (vanish) {   // Vanish Cap: washed out, ghostly
				for (int k2 = 0; k2 < 3; k2++) b.color[k2] = static_cast<std::uint8_t>(b.color[k2] / 3 + 170);
			}

			if (textured) {
				d = b;
				d.uv[0] = XMConvertFloatToHalf(a_geo.uv[i * 2]);
				d.uv[1] = XMConvertFloatToHalf(a_geo.uv[i * 2 + 1]);
				d.color[0] = d.color[1] = d.color[2] = 255;
			} else {
				d = Vertex{};
			}
		}
	}
	if (Config::renderTest == 1) {
		// a 100-unit red cube on Mario's spot, through the same upload path
		static const float c[8][3] = { { -50, -50, 0 }, { 50, -50, 0 }, { 50, 50, 0 }, { -50, 50, 0 }, { -50, -50, 100 }, { 50, -50, 100 }, { 50, 50, 100 }, { -50, 50, 100 } };
		static const int   f[12][3] = { { 0, 2, 1 }, { 0, 3, 2 }, { 4, 5, 6 }, { 4, 6, 7 }, { 0, 1, 5 }, { 0, 5, 4 }, { 1, 2, 6 }, { 1, 6, 5 }, { 2, 3, 7 }, { 2, 7, 6 }, { 3, 0, 4 }, { 3, 4, 7 } };
		for (int t = 0; t < 12; t++)
			for (int k = 0; k < 3; k++) {
				auto& v = s_body.verts[t * 3 + k];
				v.pos[0] = c[f[t][k]][0]; v.pos[1] = c[f[t][k]][1]; v.pos[2] = c[f[t][k]][2];
				v.color[0] = 255; v.color[1] = 0; v.color[2] = 0; v.color[3] = 255;
			}
	}
	Upload(s_body);
	Upload(s_decals);
	s_drawn = n;

	if (Config::renderTest == 1) {
		// and a vanilla broom under the same parent, 80 units in front
		static RE::NiPointer<RE::NiNode> s_broom;
		if (!s_broom && s_root->parent) {
			RE::BSModelDB::DBTraits::ArgsType bargs{};
			RE::BSModelDB::Demand(R"(clutter\common\broom.nif)", s_broom, bargs);
			if (s_broom) s_root->parent->AttachChild(s_broom.get(), true);
		}
		if (s_broom && s_broom->parent) {
			const auto& pw = s_broom->parent->world;
			s_broom->local.rotate = RE::NiMatrix3{};
			s_broom->local.translate = pw.rotate.Transpose() * (a_root + RE::NiPoint3{ 0, 80, 40 } - pw.translate);
			RE::NiUpdateData bctx{};
			s_broom->Update(bctx);
		}
	}

	// the vertices are offsets from Mario's position and the player's node sits exactly there, so the node itself
	// needs no offset (the parent's world transform is last frame's until the scene updates: an offset from it lags)
	if (auto parent = s_root->parent) {
		const auto& pw = parent->world;
		const float s = pw.scale != 0.0f ? pw.scale : 1.0f;
		s_root->local.rotate = pw.rotate.Transpose();
		s_root->local.translate = RE::NiPoint3{};
		s_root->local.scale = 1.0f / s;
	}
	RE::NiUpdateData ctx{};
	s_root->Update(ctx);

	static int s_debugFrames = 0;
	if (Config::telemetry && s_debugFrames++ % 300 == 0) {
		auto& wb = s_body.geometry->worldBound;
		logger::info("render debug: parent={} parentWorld=({:.0f},{:.0f},{:.0f}) world=({:.0f},{:.0f},{:.0f}) body bound=({:.0f},{:.0f},{:.0f}) r={:.0f} flags root={:X} body={:X} v0=({:.1f},{:.1f},{:.1f})",
			s_root->parent ? s_root->parent->name.c_str() : "-", s_root->parent ? s_root->parent->world.translate.x : 0.f,
			s_root->parent ? s_root->parent->world.translate.y : 0.f, s_root->parent ? s_root->parent->world.translate.z : 0.f,
			s_root->world.translate.x, s_root->world.translate.y, s_root->world.translate.z, wb.center.x, wb.center.y, wb.center.z, wb.radius,
			s_root->GetFlags().underlying(), s_body.geometry->GetFlags().underlying(), s_body.verts[0].pos[0], s_body.verts[0].pos[1], s_body.verts[0].pos[2]);
	}
}
