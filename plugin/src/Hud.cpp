#include "Hud.h"
#include "Config.h"

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
	};
	std::mutex s_lock;
	State      s_state;

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
				const float x0 = q.x / W * 2 - 1, x1 = (q.x + q.w) / W * 2 - 1;
				const float y0 = 1 - q.y / H * 2, y1 = 1 - (q.y + q.h) / H * 2;
				Vtx quad[6] = { { x0, y0, q.u0, q.v0 }, { x1, y0, q.u1, q.v0 }, { x0, y1, q.u0, q.v1 },
					{ x1, y0, q.u1, q.v0 }, { x1, y1, q.u1, q.v1 }, { x0, y1, q.u0, q.v1 } };
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

	const Image* Img(const std::string& a_name)
	{
		auto it = s_images.find(a_name);
		return it != s_images.end() && it->second.srv ? &it->second : nullptr;
	}

	// the HUD in SM64 pixels scaled to the screen: coins and stars top left, as SM64; Skyrim's three meters bottom right,
	// POWER (health) at the corner, the stamina and magicka in small head frames to its left
	constexpr float kBlockW = 140.0f, kBlockH = 64.0f, kMargin = 6.0f;
	std::vector<Quad> Layout(const State& a_s, float a_width, float a_height)
	{
		std::vector<Quad> q;
		const float s = a_height / 240.0f * 0.75f;   // SM64 pixels -> screen
		const float tlX = 14 * s, tlY = 12 * s;
		const float brX = a_width - (kBlockW + kMargin) * s, brY = a_height - (kBlockH + kMargin) * s;
		auto add = [&](const std::string& a_name, float ox, float oy, float x, float y, float w, float h) {
			if (auto im = Img(a_name)) q.push_back({ im, ox + x * s, oy + y * s, w * s, h * s });
		};
		auto top = [&](const std::string& a_name, float x, float y, float w, float h) { add(a_name, tlX, tlY, x, y, w, h); };
		auto bottom = [&](const std::string& a_name, float x, float y, float w, float h) { add(a_name, brX, brY, x, y, w, h); };

		// coins and stars, top left
		auto counter = [&](const char* a_icon, int a_value, float y) {
			top(a_icon, 72, y, 16, 16);
			top("times", 88, y, 16, 16);
			auto digits = std::to_string(std::clamp(a_value, 0, 9999));
			for (std::size_t i = 0; i < digits.size(); i++) top(std::string("digit_") + digits[i], 104 + 12.0f * i, y, 16, 16);
		};
		counter("coin", a_s.coins, 8);
		counter("star", a_s.stars, 30);

		// bottom right: stamina and magicka in small copies of the POWER meter's Mario-head frame (to be given a background of
		// their own), then the POWER meter at the corner
		constexpr float kFrame = 38.0f, kPie = kFrame / 2.0f, kTop = 64.0f - kFrame;
		auto miniMeter = [&](float x, int a_wedges, const char* a_key) {
			bottom("meter_left", x, kTop, kFrame / 2.0f, kFrame);
			bottom("meter_right", x + kFrame / 2.0f, kTop, kFrame / 2.0f, kFrame);
			if (a_wedges > 0) bottom("pie_" + std::to_string(a_wedges) + "|" + a_key, x + kFrame / 4.0f, kTop + kFrame / 4.0f, kPie, kPie);
		};
		miniMeter(0, a_s.stamina, kStamina.key);
		miniMeter(kFrame, a_s.magicka, kMagicka.key);
		bottom("meter_left", 76, 0, 32, 64);
		bottom("meter_right", 108, 0, 32, 64);
		if (a_s.wedges > 0) bottom("pie_" + std::to_string(std::clamp(a_s.wedges, 1, 8)) + "|health", 92, 16, 32, 32);
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

	HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* a_swap, UINT a_sync, UINT a_flags)
	{
		State st;
		{
			std::scoped_lock l(s_lock);
			st = s_state;
		}
		if (st.show && !s_gpuFailed) {
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
				Draw(a_swap, Layout(st, static_cast<float>(d.BufferDesc.Width), static_cast<float>(d.BufferDesc.Height)));
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

	// Skyrim's own health, stamina and magicka bars are the HUD's Flash meters: hidden while Mario is out, since
	// the power meters show the same things. Re-applied now and then (the HUD shows them again on changes).
	void SetVanillaMetersVisible(bool a_visible)
	{
		auto ui = RE::UI::GetSingleton();
		auto menu = ui ? ui->GetMenu<RE::HUDMenu>() : nullptr;
		if (!menu) return;
		// the three meters are movie clips of the HUD's base object, which also holds the compass and the shout
		// meter: only these clips are hidden (instance names in the game's hudmenu.swf)
		auto& rd = menu->GetRuntimeData();
		if (!rd.health || !rd.health->root.IsDisplayObject()) return;
		RE::GFxValue& base = rd.health->root;
		for (const char* clip : { "Health", "Stamina", "Magica" }) {
			RE::GFxValue meter;
			if (!base.GetMember(clip, &meter) || !meter.IsDisplayObject()) {
				if (Config::telemetry) logger::info("hud: no vanilla meter clip {}", clip);
				continue;
			}
			RE::GFxValue::DisplayInfo info;
			if (!meter.GetDisplayInfo(&info)) continue;
			info.SetVisible(a_visible);
			meter.SetDisplayInfo(info);
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

bool Hud::Init(const std::uint8_t* a_rom, std::size_t a_len)
{
	HMODULE dll = ::GetModuleHandleW(L"sm64.dll");
	auto extract = dll ? reinterpret_cast<int (*)(const std::uint8_t*, std::size_t)>(::GetProcAddress(dll, "tes64_hud_extract")) : nullptr;
	auto image = dll ? reinterpret_cast<int (*)(const char*, const std::uint8_t**, int*, int*)>(::GetProcAddress(dll, "tes64_hud_image")) : nullptr;
	if (!extract || !image || !extract(a_rom, a_len)) {
		logger::error("hud: couldn't read SM64's HUD graphics from the ROM");
		return false;
	}
	std::vector<std::string> names = { "times", "coin", "mario", "star", "meter_left", "meter_right" };
	for (int i = 0; i <= 9; i++) names.push_back("digit_" + std::to_string(i));
	for (int i = 1; i <= 8; i++) names.push_back("pie_" + std::to_string(i));
	for (auto& n : names) {
		const std::uint8_t* px = nullptr;
		int                 w = 0, h = 0;
		if (image(n.c_str(), &px, &w, &h)) s_images[n] = Image{ w, h, std::vector<std::uint8_t>(px, px + 4ull * w * h), nullptr };
	}

	// the wedges in each colour (health's POWER wedges are red too)
	for (const Tint* t : { &kHealth, &kStamina, &kMagicka }) {
		for (int i = 1; i <= 8; i++) {
			const std::string n = "pie_" + std::to_string(i);
			auto              it = s_images.find(n);
			if (it != s_images.end()) s_images[n + "|" + t->key] = Tinted(it->second, *t);
		}
	}
	logger::info("hud: {} graphics from the ROM", s_images.size());
	return !s_images.empty();
}

void Hud::RequestSnapshot() { s_snapRequested = true; }

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
			SetVanillaMetersVisible(false);
			s_vanillaMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		}
		s_vanillaHidden = true;
	} else if (s_vanillaHidden) {
		SetVanillaMetersVisible(true);
		s_vanillaHidden = false;
	}

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
