#include "Hud.h"
#include "Config.h"
#include "Sm64.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <fstream>
#include <unordered_map>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
	// ---- state the main thread hands the render thread --------------------------------------------------------
	struct State
	{
		bool show = false;
		int  wedges = 8;     // POWER (health): SM64's own colours by level
		int  stamina = 0;    // wedges of 8
		int  magicka = 0;
		int  coins = 0;
		int  stars = 0;
		float heading = 0.0f;   // the camera's, radians: 0 north, + clockwise (east)
		bool compass = false;
		float wipe = -1.0f;   // the Bowser screen wipe, 0 (open) to 1 (closed); below 0: none
		// where Skyrim's compass is on screen (pixels; x1 <= x0: not found): Mario's is drawn there, under its markers
		float compassX0 = 0.0f, compassX1 = 0.0f, compassY = 0.0f, compassPerRadian = 0.0f;
	};
	std::mutex s_lock;
	State      s_state;
	bool       s_forceWipe = false;   // debug: the wipe without dying
	// SOUND_OBJ_BOWSER_LAUGH: bank 5, id 0x38, priority 0x80 (what SM64 plays as the wipe starts)
	constexpr std::uint32_t kSoundBowserLaugh = (5u << 28) | (0u << 24) | (0x38u << 16) | (0x80u << 8) | (8u << 4) | 1u;

	// ---- graphics from the ROM --------------------------------------------------------------------------------
	struct Image
	{
		int                       w = 0, h = 0;
		std::vector<std::uint8_t> rgba;
		ID3D11ShaderResourceView* srv = nullptr;
	};
	std::unordered_map<std::string, Image> s_images;

	// the wedges are recoloured (keeps the ROM sprite's shading, replaces its hue): health red, stamina green, magicka blue
	struct Tint
	{
		const char* key;
		std::uint8_t r, g, b;
	};
	constexpr Tint kHealth{ "health", 240, 50, 50 };
	constexpr Tint kStamina{ "stamina", 60, 220, 60 };
	constexpr Tint kMagicka{ "magicka", 40, 120, 255 };

	// ---- D3D11 -----------------------------------------------------------------------------------------------
	using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
	Present_t s_present = nullptr;

	ID3D11Device*             s_device = nullptr;
	ID3D11DeviceContext*      s_context = nullptr;
	ID3D11VertexShader*       s_vs = nullptr;
	ID3D11PixelShader*        s_ps = nullptr;
	ID3D11InputLayout*        s_layout = nullptr;
	ID3D11Buffer*             s_vb = nullptr;
	ID3D11BlendState*         s_blend = nullptr;
	ID3D11SamplerState*       s_sampler = nullptr;
	ID3D11RasterizerState*    s_raster = nullptr;
	ID3D11DepthStencilState*  s_depth = nullptr;
	bool                      s_gpuReady = false, s_gpuFailed = false;

	struct Vtx
	{
		float x, y, u, v;
	};
	constexpr int kMaxQuads = 64;

	const char* kShader = R"(
		struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD; };
		struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };
		Texture2D tex : register(t0);
		SamplerState smp : register(s0);
		PSIn vs(VSIn i) { PSIn o; o.pos = float4(i.pos, 0, 1); o.uv = i.uv; return o; }
		float4 ps(PSIn i) : SV_TARGET { return tex.Sample(smp, i.uv); }
	)";

	bool CreateGpu(IDXGISwapChain* a_swap)
	{
		if (FAILED(a_swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&s_device)))) return false;
		s_device->GetImmediateContext(&s_context);

		ID3DBlob *vsb = nullptr, *psb = nullptr, *err = nullptr;
		if (FAILED(D3DCompile(kShader, std::strlen(kShader), nullptr, nullptr, nullptr, "vs", "vs_4_0", 0, 0, &vsb, &err)) ||
			FAILED(D3DCompile(kShader, std::strlen(kShader), nullptr, nullptr, nullptr, "ps", "ps_4_0", 0, 0, &psb, &err))) {
			logger::error("hud: shader didn't compile: {}", err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
			return false;
		}
		s_device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &s_vs);
		s_device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &s_ps);
		D3D11_INPUT_ELEMENT_DESC el[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		s_device->CreateInputLayout(el, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &s_layout);
		vsb->Release();
		psb->Release();

		D3D11_BUFFER_DESC bd{ sizeof(Vtx) * 6 * kMaxQuads, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
		s_device->CreateBuffer(&bd, nullptr, &s_vb);

		D3D11_BLEND_DESC blend{};
		blend.RenderTarget[0] = { TRUE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD,
			D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL };
		s_device->CreateBlendState(&blend, &s_blend);
		D3D11_SAMPLER_DESC sd{};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;   // SM64's chunky pixels
		sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		s_device->CreateSamplerState(&sd, &s_sampler);
		D3D11_RASTERIZER_DESC rd{};
		rd.FillMode = D3D11_FILL_SOLID;
		rd.CullMode = D3D11_CULL_NONE;
		rd.DepthClipEnable = TRUE;
		s_device->CreateRasterizerState(&rd, &s_raster);
		D3D11_DEPTH_STENCIL_DESC dd{};
		s_device->CreateDepthStencilState(&dd, &s_depth);

		for (auto& [name, im] : s_images) {
			D3D11_TEXTURE2D_DESC td{ static_cast<UINT>(im.w), static_cast<UINT>(im.h), 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
			D3D11_SUBRESOURCE_DATA init{ im.rgba.data(), static_cast<UINT>(im.w * 4), 0 };
			ID3D11Texture2D* tex = nullptr;
			if (SUCCEEDED(s_device->CreateTexture2D(&td, &init, &tex))) {
				s_device->CreateShaderResourceView(tex, nullptr, &im.srv);
				tex->Release();
			}
		}
		return s_vs && s_ps && s_layout && s_vb && s_blend && s_sampler && s_raster && s_depth;
	}

	// ---- drawing: quads in pixels, one texture per draw ------------------------------------------------------
	struct Quad
	{
		const Image* image;
		float        x, y, w, h;
		float        u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;   // part of the texture (a cropped frame)
		float        angle = 0.0f;                                  // radians, clockwise on screen, about the quad's centre
	};

	void Draw(IDXGISwapChain* a_swap, const std::vector<Quad>& a_quads)
	{
		ID3D11Texture2D* back = nullptr;
		if (FAILED(a_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) return;
		D3D11_TEXTURE2D_DESC bdesc{};
		back->GetDesc(&bdesc);
		ID3D11RenderTargetView* rtv = nullptr;
		s_device->CreateRenderTargetView(back, nullptr, &rtv);
		back->Release();
		if (!rtv) return;

		// save what Skyrim had bound
		ID3D11RenderTargetView*  oldRtv = nullptr;
		ID3D11DepthStencilView*  oldDsv = nullptr;
		ID3D11BlendState*        oldBlend = nullptr;
		FLOAT                    oldFactor[4];
		UINT                     oldMask = 0;
		ID3D11DepthStencilState* oldDepth = nullptr;
		UINT                     oldRef = 0;
		ID3D11RasterizerState*   oldRaster = nullptr;
		D3D11_VIEWPORT           oldVp[16];
		UINT                     nVp = 16;
		ID3D11InputLayout*       oldLayout = nullptr;
		D3D11_PRIMITIVE_TOPOLOGY oldTopo;
		ID3D11Buffer*            oldVb = nullptr;
		UINT                     oldStride = 0, oldOffset = 0;
		ID3D11VertexShader*      oldVs = nullptr;
		ID3D11PixelShader*       oldPs = nullptr;
		ID3D11ShaderResourceView* oldSrv = nullptr;
		ID3D11SamplerState*      oldSampler = nullptr;
		auto c = s_context;
		c->OMGetRenderTargets(1, &oldRtv, &oldDsv);
		c->OMGetBlendState(&oldBlend, oldFactor, &oldMask);
		c->OMGetDepthStencilState(&oldDepth, &oldRef);
		c->RSGetState(&oldRaster);
		c->RSGetViewports(&nVp, oldVp);
		c->IAGetInputLayout(&oldLayout);
		c->IAGetPrimitiveTopology(&oldTopo);
		c->IAGetVertexBuffers(0, 1, &oldVb, &oldStride, &oldOffset);
		c->VSGetShader(&oldVs, nullptr, nullptr);
		c->PSGetShader(&oldPs, nullptr, nullptr);
		c->PSGetShaderResources(0, 1, &oldSrv);
		c->PSGetSamplers(0, 1, &oldSampler);

		const float W = static_cast<float>(bdesc.Width), H = static_cast<float>(bdesc.Height);
		D3D11_VIEWPORT vp{ 0, 0, W, H, 0, 1 };
		c->OMSetRenderTargets(1, &rtv, nullptr);
		const float factor[4] = { 0, 0, 0, 0 };
		c->OMSetBlendState(s_blend, factor, 0xFFFFFFFF);
		c->OMSetDepthStencilState(s_depth, 0);
		c->RSSetState(s_raster);
		c->RSSetViewports(1, &vp);
		c->IASetInputLayout(s_layout);
		c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		UINT stride = sizeof(Vtx), offset = 0;
		c->IASetVertexBuffers(0, 1, &s_vb, &stride, &offset);
		c->VSSetShader(s_vs, nullptr, 0);
		c->PSSetShader(s_ps, nullptr, 0);
		c->PSSetSamplers(0, 1, &s_sampler);

		D3D11_MAPPED_SUBRESOURCE m{};
		const int n = std::min<int>(static_cast<int>(a_quads.size()), kMaxQuads);
		if (SUCCEEDED(c->Map(s_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
			auto v = static_cast<Vtx*>(m.pData);
			for (int i = 0; i < n; i++) {
				const auto& q = a_quads[i];
				// corners in pixels, turned about the centre, then to clip space
				const float cx = q.x + q.w / 2, cy = q.y + q.h / 2, ca = std::cos(q.angle), sa = std::sin(q.angle);
				auto corner = [&](float px, float py, float u, float v) {
					const float dx = px - cx, dy = py - cy;
					const float rx = cx + dx * ca - dy * sa, ry = cy + dx * sa + dy * ca;
					return Vtx{ rx / W * 2 - 1, 1 - ry / H * 2, u, v };
				};
				const Vtx tl = corner(q.x, q.y, q.u0, q.v0), tr = corner(q.x + q.w, q.y, q.u1, q.v0),
				          bl = corner(q.x, q.y + q.h, q.u0, q.v1), br = corner(q.x + q.w, q.y + q.h, q.u1, q.v1);
				Vtx quad[6] = { tl, tr, bl, tr, br, bl };
				std::memcpy(v + i * 6, quad, sizeof(quad));
			}
			c->Unmap(s_vb, 0);
			for (int i = 0; i < n; i++) {
				c->PSSetShaderResources(0, 1, &a_quads[i].image->srv);
				c->Draw(6, i * 6);
			}
		}

		// put Skyrim's state back
		c->OMSetRenderTargets(1, &oldRtv, oldDsv);
		c->OMSetBlendState(oldBlend, oldFactor, oldMask);
		c->OMSetDepthStencilState(oldDepth, oldRef);
		c->RSSetState(oldRaster);
		c->RSSetViewports(nVp, oldVp);
		c->IASetInputLayout(oldLayout);
		c->IASetPrimitiveTopology(oldTopo);
		c->IASetVertexBuffers(0, 1, &oldVb, &oldStride, &oldOffset);
		c->VSSetShader(oldVs, nullptr, 0);
		c->PSSetShader(oldPs, nullptr, 0);
		c->PSSetShaderResources(0, 1, &oldSrv);
		c->PSSetSamplers(0, 1, &oldSampler);
		for (IUnknown* u : std::initializer_list<IUnknown*>{ oldRtv, oldDsv, oldBlend, oldDepth, oldRaster, oldLayout, oldVb, oldVs, oldPs, oldSrv, oldSampler })
			if (u) u->Release();
		rtv->Release();
	}

	const Image* Img(const std::string& a_name);

	// SM64's death wipe: the screen goes black but for Bowser's face, which closes to nothing
	std::vector<Quad> WipeLayout(float a_t, float W, float H)
	{
		std::vector<Quad> q;
		auto face = Img("wipe_bowser_big"), black = Img("wipe_black");
		if (!face || !black) return q;
		const float e = a_t * a_t * (3.0f - 2.0f * a_t);   // eased
		const float size = H * 3.2f * (1.0f - e);
		const float cx = W / 2, cy = H / 2;
		const float fx = cx - size / 2, fy = cy - size / 2;
		auto rect = [&](float x, float y, float w, float h) {
			if (w > 0.5f && h > 0.5f) q.push_back({ black, x, y, w, h });
		};
		if (size < 2.0f) {
			rect(0, 0, W, H);
			return q;
		}
		q.push_back({ face, fx, fy, size, size });
		rect(0, 0, std::max(0.0f, fx), H);
		rect(fx + size, 0, W - fx - size, H);
		rect(std::max(0.0f, fx), 0, size, std::max(0.0f, fy));
		rect(std::max(0.0f, fx), fy + size, size, H - fy - size);
		return q;
	}

	const Image* Img(const std::string& a_name)
	{
		auto it = s_images.find(a_name);
		return it != s_images.end() && it->second.srv ? &it->second : nullptr;
	}

	// the HUD in SM64 pixels scaled to the screen: coins and stars top left, as SM64; Skyrim's three meters bottom right,
	// POWER (health) at the corner, the stamina and magicka in small head frames to its left
	constexpr float kBlockW = 144.0f, kBlockH = 64.0f, kMargin = 6.0f;
	std::vector<Quad> Layout(const State& a_s, float a_width, float a_height)
	{
		std::vector<Quad> q;
		const float s = a_height / 240.0f * 0.75f * Config::hudScale;   // SM64 pixels -> screen
		const float blX = kMargin * s, blY = a_height - (kBlockH + kMargin) * s;
		auto add = [&](const std::string& a_name, float ox, float oy, float x, float y, float w, float h) {
			if (auto im = Img(a_name)) q.push_back({ im, ox + x * s, oy + y * s, w * s, h * s });
		};
		auto bottom = [&](const std::string& a_name, float x, float y, float w, float h) { add(a_name, blX, blY, x, y, w, h); };

		// coins and stars, top right (clear of Skyrim's notifications at the top left), right-aligned to the corner
		auto counter = [&](const char* a_icon, int a_value, float y) {
			auto        digits = std::to_string(std::clamp(a_value, 0, 9999));
			const float w = 32.0f + 12.0f * digits.size() + 4.0f;
			const float ox = a_width - (w + kMargin + 4.0f) * s, oy = (kMargin + 2.0f) * s;
			add(a_icon, ox, oy, 0, y, 16, 16);
			add("times", ox, oy, 16, y, 16, 16);
			for (std::size_t i = 0; i < digits.size(); i++) add(std::string("digit_") + digits[i], ox, oy, 32 + 12.0f * i, y, 16, 16);
		};
		counter("star", a_s.stars, 0);
		counter("coin", a_s.coins, 20);

		// a word in the HUD font along an arc, the letters fanned out like POWER's were (each turned a little more than
		// the last, the middle ones raised), in a bar's colour; x is the word's centre, y the top of the letters
		auto label = [&](const std::string& a_word, const char* a_key, float cx, float y, float a_size, float a_advance) {
			const int n = static_cast<int>(a_word.size());
			for (int i = 0; i < n; i++) {
				const float t = i - (n - 1) / 2.0f;
				auto        im = Img("letter_" + std::string(1, a_word[i]) + "|" + a_key);
				if (!im) continue;
				Quad quad{ im, blX + (cx + t * a_advance - a_size / 2) * s, blY + (y - 0.7f * (a_size / 12.0f) * (3.0f - 0.6f * t * t)) * s, a_size * s, a_size * s };
				quad.angle = t * 0.13f;
				q.push_back(quad);
			}
		};

		// bottom left: the POWER meter (LIFE) at the corner, then magicka and stamina in small copies of its Mario-head
		// frame (Skyrim's own order: magicka, health, stamina)
		bottom("meter_left", 0, 0, 32, 64);
		bottom("meter_right", 32, 0, 32, 64);
		if (a_s.wedges > 0) bottom("pie_" + std::to_string(std::clamp(a_s.wedges, 1, 8)) + "|health", 16, 16, 32, 32);
		label("LIFE", "health", 32, 7.0f, 13.0f, 11.5f);
		constexpr float kFrame = 38.0f, kPie = kFrame / 2.0f, kTop = 64.0f - kFrame;
		auto miniMeter = [&](float x, int a_wedges, const char* a_key, const char* a_word) {
			bottom("meter_left", x, kTop, kFrame / 2.0f, kFrame);
			bottom("meter_right", x + kFrame / 2.0f, kTop, kFrame / 2.0f, kFrame);
			if (a_wedges > 0) bottom("pie_" + std::to_string(a_wedges) + "|" + a_key, x + kFrame / 4.0f, kTop + kFrame / 4.0f, kPie, kPie);
			label(a_word, a_key, x + kFrame / 2, kTop + 4.0f, 7.5f, 5.3f);
		};
		miniMeter(66, a_s.magicka, kMagicka.key, "MAGIKA");
		miniMeter(66 + kFrame + 2, a_s.stamina, kStamina.key, "STAMINA");

		return q;
	}

	// the compass, where Skyrim's was (its frame and letters hidden, its markers kept and drawn over this): a thin wooden
	// bar in the POWER frame's wood, SM64's letters for the four points sliding past as the camera turns, coins for the
	// in-between points. Spaced as Skyrim's compass is, so its markers line up.
	std::vector<Quad> CompassLayout(const State& a_s, float a_width, float a_height)
	{
		std::vector<Quad> q;
		if (!a_s.compass) return q;
		const float s = a_height / 240.0f * 0.75f * Config::hudScale;
		constexpr float kBarH = 12.0f;
		float x0 = (a_width - 176.0f * s) / 2, x1 = x0 + 176.0f * s, yc = (kMargin + kBarH / 2) * s;
		float perRadian = (x1 - x0) / 2 / RE::NI_HALF_PI;
		if (a_s.compassX1 > a_s.compassX0 + 20.0f) {
			x0 = a_s.compassX0;
			x1 = a_s.compassX1;
			yc = a_s.compassY;
			if (a_s.compassPerRadian > 0) perRadian = a_s.compassPerRadian;   // Skyrim's own scale: its markers line up
		}
		const float barW = x1 - x0, barH = kBarH * s, top = yc - barH / 2;
		auto put = [&](const char* a_name, float x, float y, float w, float h) {
			if (auto im = Img(a_name)) q.push_back({ im, x, y, w, h });
		};
		put("compass_bar", x0, top, barW, barH);
		static const struct { const char* name; float angle; float size; } points[] = {
			{ "letter_N", 0.0f, 11 }, { "letter_E", RE::NI_HALF_PI, 11 }, { "letter_S", RE::NI_PI, 11 }, { "letter_W", -RE::NI_HALF_PI, 11 },
			{ "coin", RE::NI_PI / 4, 6 }, { "coin", 3 * RE::NI_PI / 4, 6 }, { "coin", -RE::NI_PI / 4, 6 }, { "coin", -3 * RE::NI_PI / 4, 6 },
		};
		for (auto& p : points) {
			float rel = p.angle - a_s.heading;
			while (rel > RE::NI_PI) rel -= RE::NI_TWO_PI;
			while (rel < -RE::NI_PI) rel += RE::NI_TWO_PI;
			const float cx = x0 + barW / 2 + rel * perRadian, sz = p.size * s;
			if (cx - sz / 2 < x0 + 4 * s || cx + sz / 2 > x1 - 4 * s) continue;
			put(p.name, cx - sz / 2, yc - sz / 2, sz, sz);
		}
		put("compass_pointer", x0 + barW / 2 - 4 * s, top + barH - 2 * s, 8 * s, 6 * s);
		return q;
	}

	// time the HUD takes per frame (telemetry: logged with the HUD line, once a second)
	UINT   s_backWidth = 0, s_backHeight = 0;
	double s_drawMs = 0.0;
	int    s_drawFrames = 0;

	std::atomic<bool> s_snapRequested{ false };

	// the back buffer as it is when the HUD is drawn: a BMP next to the logs (what the screen captures can't show)
	void Snapshot(IDXGISwapChain* a_swap)
	{
		ID3D11Texture2D* back = nullptr;
		if (FAILED(a_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) return;
		D3D11_TEXTURE2D_DESC d{};
		back->GetDesc(&d);
		const bool rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM;
		d.Usage = D3D11_USAGE_STAGING;
		d.BindFlags = 0;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		d.MiscFlags = 0;
		ID3D11Texture2D* staging = nullptr;
		D3D11_MAPPED_SUBRESOURCE m{};
		if (SUCCEEDED(s_device->CreateTexture2D(&d, nullptr, &staging))) {
			s_context->CopyResource(staging, back);
			if (SUCCEEDED(s_context->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
				const auto dir = logger::log_directory();
				if (dir) {
					const auto  path = *dir / "TES64_snap.bmp";
					const UINT  w = d.Width, h = d.Height;
					std::ofstream f(path, std::ios::binary);
					std::uint8_t hdr[54] = {};
					const std::uint32_t size = 54 + w * h * 4;
					std::memcpy(hdr, "BM", 2);
					std::memcpy(hdr + 2, &size, 4);
					const std::uint32_t off = 54, info = 40, planes_bpp = 1 | (32 << 16);
					std::memcpy(hdr + 10, &off, 4);
					std::memcpy(hdr + 14, &info, 4);
					const std::int32_t iw = static_cast<std::int32_t>(w), ih = static_cast<std::int32_t>(h);
					std::memcpy(hdr + 18, &iw, 4);
					std::memcpy(hdr + 22, &ih, 4);
					std::memcpy(hdr + 26, &planes_bpp, 4);
					f.write(reinterpret_cast<const char*>(hdr), 54);
					for (int y = static_cast<int>(h) - 1; y >= 0; y--) {   // BMP rows run bottom up
						const auto* row = static_cast<const std::uint8_t*>(m.pData) + static_cast<std::size_t>(y) * m.RowPitch;
						for (UINT x = 0; x < w; x++) {
							std::uint8_t px[4] = { row[x * 4 + 2], row[x * 4 + 1], row[x * 4], 255 };   // BGRA
							if (rgba) { px[0] = row[x * 4 + 2]; px[2] = row[x * 4]; }
							f.write(reinterpret_cast<const char*>(px), 4);
						}
					}
					logger::info("hud: snapshot written ({}x{})", w, h);
				}
				s_context->Unmap(staging, 0);
			}
			staging->Release();
		}
		back->Release();
	}

	IDXGISwapChain* s_swap = nullptr;

	// Skyrim's compass frame on screen: its bounds on the stage (ActionScript getBounds), then the stage's visible
	// rectangle onto the back buffer
	// Skyrim's compass strip scrolls 174.4 stage units per radian the camera turns (measured, hudmenu.swf)
	constexpr float kCompassStagePerRadian = 174.4f;

	bool CompassOnScreen(float& a_x0, float& a_x1, float& a_y, float& a_perRadian)
	{
		auto ui = RE::UI::GetSingleton();
		auto menu = ui ? ui->GetMenu<RE::HUDMenu>() : nullptr;
		if (!menu || !menu->uiMovie || !s_backWidth) return false;
		auto& rd = menu->GetRuntimeData();
		if (!rd.health || !rd.health->root.IsDisplayObject()) return false;
		RE::GFxValue holder, compass, frame, stage, bounds;
		if (!rd.health->root.GetMember("CompassShoutMeterHolder", &holder) || !holder.GetMember("Compass", &compass) || !compass.GetMember("CompassFrame", &frame)) return false;
		if (!menu->uiMovie->GetVariable(&stage, "_root")) return false;
		if (!frame.Invoke("getBounds", &bounds, &stage, 1) || !bounds.IsObject()) return false;
		RE::GFxValue xMin, xMax, yMin, yMax;
		bounds.GetMember("xMin", &xMin); bounds.GetMember("xMax", &xMax); bounds.GetMember("yMin", &yMin); bounds.GetMember("yMax", &yMax);
		if (!xMin.IsNumber() || !xMax.IsNumber()) return false;
		const auto vis = menu->uiMovie->GetVisibleFrameRect();
		const float vw = vis.right - vis.left, vh = vis.bottom - vis.top;
		if (vw <= 0 || vh <= 0) return false;
		auto sx = [&](double x) { return static_cast<float>((x - vis.left) / vw * s_backWidth); };
		auto sy = [&](double y) { return static_cast<float>((y - vis.top) / vh * s_backHeight); };
		a_x0 = sx(xMin.GetNumber());
		a_x1 = sx(xMax.GetNumber());
		a_y = (sy(yMin.GetNumber()) + sy(yMax.GetNumber())) / 2;
		a_perRadian = kCompassStagePerRadian / vw * s_backWidth;
		return a_x1 > a_x0;
	}

	// the HUD menu's own draw: Mario's compass goes on first, so Skyrim's markers come out on top of it
	struct HudDisplayHook
	{
		static void PostDisplay(RE::HUDMenu* a_this)
		{
			State st;
			{
				std::scoped_lock l(s_lock);
				st = s_state;
			}
			if (st.show && st.compass && s_gpuReady && s_swap && s_backWidth)
				Draw(s_swap, CompassLayout(st, static_cast<float>(s_backWidth), static_cast<float>(s_backHeight)));
			original(a_this);
		}
		static inline REL::Relocation<decltype(PostDisplay)> original;
	};

	HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* a_swap, UINT a_sync, UINT a_flags)
	{
		s_swap = a_swap;
		State st;
		{
			std::scoped_lock l(s_lock);
			st = s_state;
		}
		if ((st.show || st.wipe >= 0.0f) && !s_gpuFailed) {
			if (!s_gpuReady) {
				s_gpuReady = CreateGpu(a_swap);
				s_gpuFailed = !s_gpuReady;
				logger::info("hud: {}", s_gpuReady ? "ready" : "couldn't set up D3D11, no HUD");
			}
			if (s_gpuReady) {
				DXGI_SWAP_CHAIN_DESC d{};
				a_swap->GetDesc(&d);
				const auto start = std::chrono::steady_clock::now();
				s_backWidth = d.BufferDesc.Width;
				s_backHeight = d.BufferDesc.Height;
				if (st.show) Draw(a_swap, Layout(st, static_cast<float>(d.BufferDesc.Width), static_cast<float>(d.BufferDesc.Height)));
				if (st.wipe >= 0.0f) Draw(a_swap, WipeLayout(st.wipe, static_cast<float>(d.BufferDesc.Width), static_cast<float>(d.BufferDesc.Height)));
				if (s_snapRequested.exchange(false)) Snapshot(a_swap);
				s_drawMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				s_drawFrames++;
			}
		}
		return s_present(a_swap, a_sync, a_flags);
	}

	// ---- coins / stars ---------------------------------------------------------------------------------------
	int CountGold(RE::PlayerCharacter* a_player)
	{
		auto gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F);
		if (!gold) return 0;
		auto counts = a_player->GetInventoryCounts([gold](RE::TESBoundObject& a_obj) { return &a_obj == gold; });
		auto it = counts.find(gold);
		return it != counts.end() ? it->second : 0;
	}

	int CountStars()
	{
		auto data = RE::TESDataHandler::GetSingleton();
		if (!data) return 0;
		int n = 0;
		for (auto quest : data->GetFormArray<RE::TESQuest>())
			if (quest && quest->IsCompleted()) n++;
		return n;
	}

	// a value out of its maximum, as SM64's wedges (1-8, or none at 0)
	int Wedges(float a_current, float a_max)
	{
		if (a_max <= 0.0f) return 0;
		return std::clamp(static_cast<int>(std::ceil(8.0f * a_current / a_max)), 0, 8);
	}

	// a movie clip under the HUD's base object by dotted path ("CompassShoutMeterHolder.Compass"), shown or hidden.
	// False if the clip isn't there (the instance names are hudmenu.swf's; HUD mods may rename them)
	bool SetClipVisible(const char* a_path, bool a_visible)
	{
		auto ui = RE::UI::GetSingleton();
		auto menu = ui ? ui->GetMenu<RE::HUDMenu>() : nullptr;
		if (!menu) return false;
		auto& rd = menu->GetRuntimeData();
		if (!rd.health || !rd.health->root.IsDisplayObject()) return false;
		RE::GFxValue clip = rd.health->root;
		std::string  path = a_path;
		for (std::size_t start = 0; start <= path.size();) {
			const auto  dot = path.find('.', start);
			std::string name = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
			RE::GFxValue next;
			if (!clip.GetMember(name.c_str(), &next) || !next.IsDisplayObject()) return false;
			clip = next;
			if (dot == std::string::npos) break;
			start = dot + 1;
		}
		RE::GFxValue::DisplayInfo info;
		if (!clip.GetDisplayInfo(&info)) return false;
		info.SetVisible(a_visible);
		clip.SetDisplayInfo(info);
		return true;
	}

	// debug: the display objects under a clip, a few levels down, with where they sit
	void Dump(const RE::GFxValue& a_clip, const std::string& a_path, int a_depth)
	{
		RE::GFxValue copy = a_clip;
		copy.VisitMembers([&](const char* a_name, const RE::GFxValue& a_val) {
			if (!a_val.IsDisplayObject()) return;
			RE::GFxValue::DisplayInfo info;
			RE::GFxValue              v = a_val;
			v.GetDisplayInfo(&info);
			logger::info("FLASH {}.{} x={:.0f} y={:.0f} w? vis={}", a_path, a_name, info.GetX(), info.GetY(), info.GetVisible() ? 1 : 0);
			if (a_depth > 0) Dump(a_val, a_path + "." + a_name, a_depth - 1);
		});
	}

	// Skyrim's own health, stamina and magicka bars: hidden while Mario is out, the power meters show the same things.
	// Also the crosshair with the weapons away, and the compass when Mario's own is drawn. Re-applied now and then
	// (the HUD shows them again on changes).
	void SetVanillaVisible(bool a_meters, bool a_crosshair, bool a_compass)
	{
		for (const char* clip : { "Health", "Stamina", "Magica" }) SetClipVisible(clip, a_meters);
		static bool s_logged = false;
		const bool  cross = SetClipVisible("CrosshairInstance", a_crosshair) | SetClipVisible("Crosshair", a_crosshair);
		// the compass's frame goes; its strip of markers (and faint letters, under Mario's) stays, drawn over Mario's
		// compass and still clipped to its window by its own mask
		const bool comp = SetClipVisible("CompassShoutMeterHolder.Compass.CompassFrame", a_compass);
		SetClipVisible("CompassShoutMeterHolder.Compass.CompassMask_mc", true);
		if (Config::telemetry && !s_logged) {
			logger::info("hud: crosshair clip {}, compass clip {}", cross ? "found" : "missing", comp ? "found" : "missing");
			s_logged = true;
		}
	}

	// tinted copy: the ROM's own shading, each pixel's brightness kept, its hue replaced
	Image Tinted(const Image& a_src, const Tint& a_tint)
	{
		Image out = a_src;
		out.srv = nullptr;
		// brightness relative to the sprite's own brightest pixel, so the colour comes out at full strength
		float brightest = 0.0f;
		for (std::size_t i = 0; i + 3 < out.rgba.size(); i += 4)
			if (out.rgba[i + 3]) brightest = std::max(brightest, 0.3f * out.rgba[i] + 0.59f * out.rgba[i + 1] + 0.11f * out.rgba[i + 2]);
		brightest = brightest > 0.0f ? brightest / 255.0f : 1.0f;
		for (std::size_t i = 0; i + 3 < out.rgba.size(); i += 4) {
			const float lum = (0.3f * out.rgba[i] + 0.59f * out.rgba[i + 1] + 0.11f * out.rgba[i + 2]) / 255.0f;
			const float k = std::clamp(0.3f + 0.7f * lum / brightest, 0.0f, 1.0f);
			out.rgba[i] = static_cast<std::uint8_t>(a_tint.r * k);
			out.rgba[i + 1] = static_cast<std::uint8_t>(a_tint.g * k);
			out.rgba[i + 2] = static_cast<std::uint8_t>(a_tint.b * k);
		}
		return out;
	}
}

// the compass bar: a rounded plank in the POWER frame's wood (tan, lighter on top, a dark brown outline), and a small
// red pointer for the middle. Generated here, not from the ROM.
Image MakeCompassBar()
{
	constexpr int W = 176, H = 20;
	Image         im{ W, H, std::vector<std::uint8_t>(W * H * 4, 0), nullptr };
	const float   r = H / 2.0f;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			const float cx = std::clamp(x + 0.5f, r, W - r), cy = r;
			const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
			if (d > r) continue;
			std::uint8_t* p = &im.rgba[(y * W + x) * 4];
			if (d > r - 1.6f) { p[0] = 131; p[1] = 82; p[2] = 32; p[3] = 255; continue; }
			const float t = std::clamp(1.0f - (y / float(H)) * 0.8f + 0.08f * std::sin(x * 0.35f + 1.3f * std::sin(y * 0.7f)), 0.0f, 1.0f);
			const float dark[3] = { 238, 156, 82 }, light[3] = { 255, 230, 139 };
			for (int k = 0; k < 3; k++) p[k] = static_cast<std::uint8_t>(dark[k] + (light[k] - dark[k]) * t);
			p[3] = 230;
		}
	return im;
}

Image MakePointer()
{
	constexpr int W = 8, H = 6;
	Image         im{ W, H, std::vector<std::uint8_t>(W * H * 4, 0), nullptr };
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			if (std::abs(x + 0.5f - W / 2.0f) <= (H - y) * 0.7f) {
				std::uint8_t* p = &im.rgba[(y * W + x) * 4];
				p[0] = 230; p[1] = 30; p[2] = 30; p[3] = 255;
			}
	return im;
}

bool Hud::Init(const std::uint8_t* a_rom, std::size_t a_len)
{
	HMODULE dll = ::GetModuleHandleW(L"sm64.dll");
	auto extract = dll ? reinterpret_cast<int (*)(const std::uint8_t*, std::size_t)>(::GetProcAddress(dll, "tes64_hud_extract")) : nullptr;
	auto image = dll ? reinterpret_cast<int (*)(const char*, const std::uint8_t**, int*, int*)>(::GetProcAddress(dll, "tes64_hud_image")) : nullptr;
	if (!extract || !image || !extract(a_rom, a_len)) {
		logger::error("hud: couldn't read SM64's HUD graphics from the ROM");
		return false;
	}
	std::vector<std::string> names = { "times", "coin", "mario", "star", "meter_left", "meter_right", "wipe_bowser" };
	for (int i = 0; i <= 9; i++) names.push_back("digit_" + std::to_string(i));
	for (int i = 1; i <= 8; i++) names.push_back("pie_" + std::to_string(i));
	for (char c : std::string("AEFGIKLMNSTW")) names.push_back(std::string("letter_") + c);
	for (auto& n : names) {
		const std::uint8_t* px = nullptr;
		int                 w = 0, h = 0;
		if (image(n.c_str(), &px, &w, &h)) s_images[n] = Image{ w, h, std::vector<std::uint8_t>(px, px + 4ull * w * h), nullptr };
	}

	// the ROM's meter frame has "POWER" baked into its top: wiped (anything above the wood's tan and brown), so the
	// labels can be written in their place
	for (const char* n : { "meter_left", "meter_right" }) {
		auto it = s_images.find(n);
		if (it == s_images.end()) continue;
		auto& im = it->second;
		for (int y = 0; y < std::min(24, im.h); y++) {
			for (int x = 0; x < im.w; x++) {
				std::uint8_t* p = &im.rgba[(y * im.w + x) * 4];
				if (!p[3]) continue;
				const float r = p[0] / 255.0f, g = p[1] / 255.0f, b = p[2] / 255.0f;
				const float mx = std::max({ r, g, b }), mn = std::min({ r, g, b }), d = mx - mn;
				const float sat = mx > 0 ? d / mx : 0.0f;
				float       hue = 0.0f;
				if (d > 0) {
					if (mx == r) hue = std::fmod((g - b) / d, 6.0f) / 6.0f;
					else if (mx == g) hue = ((b - r) / d + 2.0f) / 6.0f;
					else hue = ((r - g) / d + 4.0f) / 6.0f;
					if (hue < 0) hue += 1.0f;
				}
				const bool wood = (hue >= 0.05f && hue <= 0.14f && sat >= 0.2f && sat <= 0.7f && mx > 0.55f) || (mx < 0.6f && hue >= 0.04f && hue <= 0.1f);
				if (!wood) p[3] = 0;
			}
		}
	}

	// the wedges in each colour (health's POWER wedges are red too)
	for (const Tint* t : { &kHealth, &kStamina, &kMagicka }) {
		for (int i = 1; i <= 8; i++) {
			const std::string n = "pie_" + std::to_string(i);
			auto              it = s_images.find(n);
			if (it != s_images.end()) s_images[n + "|" + t->key] = Tinted(it->second, *t);
		}
		for (char c : std::string("AEFGIKLMNST")) {
			const std::string n = std::string("letter_") + c;
			auto              it = s_images.find(n);
			if (it != s_images.end()) s_images[n + "|" + t->key] = Tinted(it->second, *t);
		}
	}
	// the Bowser wipe: the ROM's 64x64 face smoothed up (the N64 filtered it) so it can be blown up to the screen
	if (auto it = s_images.find("wipe_bowser"); it != s_images.end()) {
		const int N = 512;
		Image     big{ N, N, std::vector<std::uint8_t>(N * N * 4, 0), nullptr };
		const auto& src = it->second;
		for (int y = 0; y < N; y++)
			for (int x = 0; x < N; x++) {
				const float fx = std::clamp((x + 0.5f) * src.w / N - 0.5f, 0.0f, src.w - 1.0f), fy = std::clamp((y + 0.5f) * src.h / N - 0.5f, 0.0f, src.h - 1.0f);
				const int   x0 = static_cast<int>(fx), y0 = static_cast<int>(fy), x1 = std::min(x0 + 1, src.w - 1), y1 = std::min(y0 + 1, src.h - 1);
				const float tx = fx - x0, ty = fy - y0;
				auto        a = [&](int px, int py) { return static_cast<float>(src.rgba[(py * src.w + px) * 4 + 3]); };
				const float v = (a(x0, y0) * (1 - tx) + a(x1, y0) * tx) * (1 - ty) + (a(x0, y1) * (1 - tx) + a(x1, y1) * tx) * ty;
				big.rgba[(y * N + x) * 4 + 3] = static_cast<std::uint8_t>(v + 0.5f);
			}
		s_images["wipe_bowser_big"] = std::move(big);
	}
	s_images["wipe_black"] = Image{ 1, 1, { 0, 0, 0, 255 }, nullptr };
	s_images["compass_bar"] = MakeCompassBar();
	s_images["compass_pointer"] = MakePointer();
	logger::info("hud: {} graphics from the ROM", s_images.size());
	return !s_images.empty();
}

void Hud::DebugWipe(bool a_on) { s_forceWipe = a_on; }

void Hud::RequestSnapshot() { s_snapRequested = true; }

void Hud::DumpFlash()
{
	auto ui = RE::UI::GetSingleton();
	auto menu = ui ? ui->GetMenu<RE::HUDMenu>() : nullptr;
	if (!menu || !menu->GetRuntimeData().health) return;
	Dump(menu->GetRuntimeData().health->root, "HUD", 3);
}

void Hud::InstallHook()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto swap = renderer ? reinterpret_cast<IDXGISwapChain*>(renderer->GetRuntimeData().renderWindows[0].swapChain) : nullptr;
	if (!swap) {
		logger::error("hud: no swap chain");
		return;
	}
	auto   vtbl = *reinterpret_cast<void***>(swap);
	DWORD  old = 0;
	::VirtualProtect(&vtbl[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
	s_present = reinterpret_cast<Present_t>(vtbl[8]);
	vtbl[8] = reinterpret_cast<void*>(&PresentHook);
	::VirtualProtect(&vtbl[8], sizeof(void*), old, &old);
	logger::info("hud: Present hooked");
	REL::Relocation<std::uintptr_t> hud{ RE::VTABLE_HUDMenu[0] };
	HudDisplayHook::original = hud.write_vfunc(0x6, HudDisplayHook::PostDisplay);
}

void Hud::Tick(RE::PlayerCharacter* a_player, bool a_marioActive, int a_wedges, float a_delta)
{
	static float s_countTimer = 0.0f, s_starTimer = 0.0f, s_telTimer = 0.0f, s_vanillaTimer = 0.0f;
	static int   s_coins = -1, s_stars = 0;
	static bool  s_vanillaHidden = false;
	static double s_vanillaMs = 0.0;

	if (a_marioActive && (s_countTimer += a_delta) >= 0.25f) {
		s_countTimer = 0.0f;
		const int coins = CountGold(a_player);
		// SM64: a coin restores a quarter wedge. Gold picked up as Mario heals the same share of Skyrim health.
		if (s_coins >= 0 && coins > s_coins) {
			auto        av = a_player->AsActorValueOwner();
			const float maxHp = av->GetPermanentActorValue(RE::ActorValue::kHealth);
			const float heal = (coins - s_coins) * maxHp / 32.0f;
			av->RestoreActorValue(RE::ActorValue::kHealth, heal);
			if (Config::telemetry) logger::info("HUD coins {} -> {}: healed {:.0f}", s_coins, coins, heal);
		}
		s_coins = coins;
	}
	if (!a_marioActive) s_coins = -1;
	if ((s_starTimer += a_delta) >= 5.0f || s_starTimer == a_delta) {
		s_starTimer = 0.0f;
		s_stars = CountStars();
	}

	// stamina and magicka, out of their maxima
	int stamina = 0, magicka = 0;
	if (a_marioActive) {
		auto av = a_player->AsActorValueOwner();
		stamina = Wedges(av->GetActorValue(RE::ActorValue::kStamina), av->GetPermanentActorValue(RE::ActorValue::kStamina));
		magicka = Wedges(av->GetActorValue(RE::ActorValue::kMagicka), av->GetPermanentActorValue(RE::ActorValue::kMagicka));
	}

	// Skyrim's own bars: hidden while Mario is out (re-applied every fifth of a second)
	if (a_marioActive) {
		if ((s_vanillaTimer -= a_delta) <= 0.0f) {
			s_vanillaTimer = 0.2f;
			const auto start = std::chrono::steady_clock::now();
			const bool drawn = a_player->AsActorState()->IsWeaponDrawn();
			SetVanillaVisible(false, drawn || !Config::hideCrosshairSheathed, !Config::compass);
			float x0, x1, y, per;
			if (Config::compass && CompassOnScreen(x0, x1, y, per)) {
				std::scoped_lock l(s_lock);
				s_state.compassPerRadian = per;
				s_state.compassX0 = x0;
				s_state.compassX1 = x1;
				s_state.compassY = y;
			}
			s_vanillaMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		}
		s_vanillaHidden = true;
	} else if (s_vanillaHidden) {
		SetVanillaVisible(true, true, true);
		s_vanillaHidden = false;
	}

	// Mario dies: the Bowser wipe closes over 48 SM64 frames, with his laugh, and holds
	static float s_wipeT = -1.0f;
	if (a_marioActive && ((Config::deathWipe && a_player->IsDead()) || s_forceWipe)) {
		if (s_wipeT < 0.0f) {
			s_wipeT = 0.0f;
			if (Config::sounds && Sm64::play_sound) Sm64::play_sound(kSoundBowserLaugh);
			logger::info("hud: Mario died, Bowser wipe");
		}
		s_wipeT += a_delta;
	} else if (!s_forceWipe) {
		s_wipeT = -1.0f;
	}
	const float wipe = s_wipeT < 0.0f ? -1.0f : std::min(s_wipeT / 1.6f, 1.0f);
	auto ui = RE::UI::GetSingleton();
	const bool menus = ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
	{
		std::scoped_lock l(s_lock);
		s_state.show = a_marioActive && !menus && !s_images.empty();
		s_state.wedges = a_wedges;
		s_state.stamina = stamina;
		s_state.magicka = magicka;
		s_state.coins = std::max(0, s_coins);
		s_state.stars = s_stars;
		s_state.compass = Config::compass && a_marioActive;
		s_state.wipe = wipe;
		if (auto camera = RE::PlayerCamera::GetSingleton(); camera && camera->cameraRoot) {
			const auto& r = camera->cameraRoot->world.rotate;   // its forward is the second column
			s_state.heading = std::atan2(r.entry[0][1], r.entry[1][1]);
		}
	}
	if (Config::telemetry && a_marioActive && (s_telTimer += a_delta) >= 1.0f) {
		s_telTimer = 0.0f;
		logger::info("HUD shown={} wedges={} stamina={} magicka={} coins={} stars={} gpu={} draw={:.3f}ms", s_state.show ? 1 : 0, a_wedges, stamina, magicka, s_state.coins, s_stars, s_gpuReady ? 1 : 0,
			s_drawFrames ? s_drawMs / s_drawFrames : 0.0);
		const auto engine = RE::BSGraphics::State::GetSingleton();
		logger::info("HUD vanilla meters hide cost={:.3f}ms (per call, 5 a second), back buffer {}x{}, engine screen {}x{}, viewport {}x{}",
			s_vanillaMs / 5.0, s_backWidth, s_backHeight, engine->screenWidth, engine->screenHeight, engine->frameBufferViewport[0], engine->frameBufferViewport[1]);
		s_drawMs = 0.0;
		s_drawFrames = 0;
		s_vanillaMs = 0.0;
	}
}
