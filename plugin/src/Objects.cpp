#include "Objects.h"
#include "Config.h"
#include "Render.h"
#include "Sm64.h"

#include <d3d11.h>
#include <DirectXPackedVector.h>

namespace
{
	constexpr int kMaxTris = 512;   // objects.nif's placeholder triangles (tools/make_mario_nif.py OBJECT_TRIS)
	// objrom.h's models
	constexpr int OM_COIN_YELLOW = 0, OM_COIN_BLUE = 2, OM_STAR = 3;

	struct Vertex   // objects.nif's vertex format (as Render.cpp's: vanilla clutter's 0x3B)
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

	// a model from the ROM: SM64 units about its origin, per corner
	struct Mesh
	{
		std::vector<float> pos, nrm, uv;
		int                tris = 0;
	};
	std::vector<Mesh> s_meshes;
	int               s_star = -1, s_coins[2][4] = { { -1, -1, -1, -1 }, { -1, -1, -1, -1 } };
	bool              s_ready = false;

	RE::NiPointer<RE::NiNode> s_node;
	ID3D11Buffer*             s_buffer = nullptr;
	std::uint8_t*             s_raw = nullptr;
	std::vector<Vertex>       s_verts;
	bool                      s_attached = false;

	struct Placed
	{
		int   mesh;
		float pos[3], yaw, scale;
	};
	std::vector<Placed> s_frame;

	std::uint8_t Snorm(float f) { return static_cast<std::uint8_t>(std::clamp(f * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f); }

	bool LoadMesh()
	{
		if (s_node) return true;
		RE::BSModelDB::DBTraits::ArgsType args{};
		if (RE::BSModelDB::Demand("TES64\\objects.nif", s_node, args) != RE::BSResource::ErrorCode::kNone || !s_node) {
			logger::error("objects: couldn't load meshes/TES64/objects.nif");
			return false;
		}
		auto obj = s_node->GetObjectByName("Objects");
		auto geo = obj ? obj->AsGeometry() : nullptr;
		auto data = geo ? geo->GetGeometryRuntimeData().rendererData : nullptr;
		s_buffer = data ? reinterpret_cast<ID3D11Buffer*>(data->vertexBuffer) : nullptr;
		s_raw = data ? data->rawVertexData : nullptr;
		if (!s_buffer) {
			logger::error("objects: objects.nif has no vertex buffer");
			s_node.reset();
			return false;
		}
		s_verts.assign(kMaxTris * 3, Vertex{});
		return true;
	}
}

bool Objects::Init()
{
	HMODULE dll = ::GetModuleHandleW(L"sm64.dll");
	auto load = dll ? reinterpret_cast<int (*)(const std::uint8_t*, std::size_t)>(::GetProcAddress(dll, "tes64_obj_load")) : nullptr;
	auto error = dll ? reinterpret_cast<const char* (*)()>(::GetProcAddress(dll, "tes64_obj_error")) : nullptr;
	auto atlas = dll ? reinterpret_cast<int (*)(const std::uint8_t**, int*, int*)>(::GetProcAddress(dll, "tes64_obj_atlas")) : nullptr;
	auto mesh = dll ? reinterpret_cast<int (*)(int, int, float*, float*, float*, int)>(::GetProcAddress(dll, "tes64_obj_mesh")) : nullptr;
	if (!load || !atlas || !mesh || Sm64::rom.empty()) return false;
	if (!load(Sm64::rom.data(), Sm64::rom.size())) {
		logger::error("objects: SM64's objects couldn't be read from the ROM: {}", error ? error() : "?");
		return false;
	}
	auto add = [&](int a_model, int a_animState) {
		Mesh m;
		m.pos.resize(64 * 9);
		m.nrm.resize(64 * 9);
		m.uv.resize(64 * 6);
		m.tris = mesh(a_model, a_animState, m.pos.data(), m.nrm.data(), m.uv.data(), 64);
		s_meshes.push_back(std::move(m));
		return static_cast<int>(s_meshes.size()) - 1;
	};
	s_star = add(OM_STAR, 0);
	// the coin's switch: its four spin pictures are every other case
	for (int f = 0; f < 4; f++) {
		s_coins[0][f] = add(OM_COIN_YELLOW, f * 2);
		s_coins[1][f] = add(OM_COIN_BLUE, f * 2);
	}
	const std::uint8_t* rgba = nullptr;
	int                 w = 0, h = 0;
	s_ready = s_meshes[s_star].tris > 0 && atlas(&rgba, &w, &h) && Render::WriteDds("objects.dds", rgba, w, h);
	logger::info("objects: star ({} triangles) and coins from the ROM{}", s_meshes[s_star].tris, s_ready ? "" : " (not ready)");
	return s_ready;
}

bool Objects::Ready() { return s_ready; }
int  Objects::StarMesh() { return s_star; }
int  Objects::CoinMesh(bool a_blue, int a_frame) { return s_coins[a_blue ? 1 : 0][a_frame & 3]; }

void Objects::Begin() { s_frame.clear(); }

void Objects::Put(int a_mesh, const float a_pos[3], float a_yaw, float a_scale)
{
	if (a_mesh < 0 || a_mesh >= static_cast<int>(s_meshes.size())) return;
	s_frame.push_back({ a_mesh, { a_pos[0], a_pos[1], a_pos[2] }, a_yaw, a_scale });
}

void Objects::Hide()
{
	if (s_attached && s_node && s_node->parent) s_node->parent->DetachChild2(s_node.get());
	s_attached = false;
}

void Objects::End(RE::NiNode* a_marioRoot, const float a_marioSm64[3], float a_scale)
{
	if (s_frame.empty() || !s_ready || !a_marioRoot) {
		Hide();
		return;
	}
	if (!LoadMesh()) return;
	if (!s_attached || s_node->parent != a_marioRoot) {
		Hide();
		a_marioRoot->AttachChild(s_node.get(), true);
		s_attached = true;
	}
	using DirectX::PackedVector::XMConvertFloatToHalf;
	int t = 0;
	for (auto& o : s_frame) {
		const auto& m = s_meshes[o.mesh];
		const float c = std::cos(o.yaw), s = std::sin(o.yaw);
		// relative to Mario's node (which sits at Mario, unturned): SM64 offset -> Skyrim axes
		const float rel[3] = { o.pos[0] - a_marioSm64[0], o.pos[1] - a_marioSm64[1], o.pos[2] - a_marioSm64[2] };
		for (int mt = 0; mt < m.tris && t < kMaxTris; mt++, t++)
			for (int k = 0; k < 3; k++) {
				Vertex&      v = s_verts[t * 3 + k];
				const float* lp = &m.pos[(mt * 3 + k) * 3];
				const float* ln = &m.nrm[(mt * 3 + k) * 3];
				const float  x = (lp[0] * c + lp[2] * s) * o.scale, y = lp[1] * o.scale, z = (-lp[0] * s + lp[2] * c) * o.scale;
				const float  nx = ln[0] * c + ln[2] * s, ny = ln[1], nz = -ln[0] * s + ln[2] * c;
				const float  sx = rel[0] + x, sy = rel[1] + y, sz = rel[2] + z;
				v.pos[0] = sx * a_scale; v.pos[1] = -sz * a_scale; v.pos[2] = sy * a_scale;
				v.bitangentX = 0.0f;
				v.uv[0] = XMConvertFloatToHalf(m.uv[(mt * 3 + k) * 2]);
				v.uv[1] = XMConvertFloatToHalf(m.uv[(mt * 3 + k) * 2 + 1]);
				v.normal[0] = Snorm(nx); v.normal[1] = Snorm(-nz); v.normal[2] = Snorm(ny); v.bitangentY = 128;
				v.tangent[0] = Snorm(1.0f); v.tangent[1] = Snorm(0.0f); v.tangent[2] = Snorm(0.0f); v.bitangentZ = 128;
				v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
			}
	}
	for (; t < kMaxTris; t++)
		for (int k = 0; k < 3; k++) s_verts[t * 3 + k] = Vertex{};
	auto context = reinterpret_cast<ID3D11DeviceContext*>(RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context);
	if (context) context->UpdateSubresource(s_buffer, 0, nullptr, s_verts.data(), 0, 0);
	if (s_raw) std::memcpy(s_raw, s_verts.data(), sizeof(Vertex) * s_verts.size());
}
