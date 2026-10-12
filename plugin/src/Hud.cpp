#include "Hud.h"
#include "Config.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <unordered_map>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
	// ---- state the main thread hands the render thread --------------------------------------------------------
	struct State
	{
		bool show = false;
		int  wedges = 8;
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
				auto& q = a_quads[i];
				float x0 = q.x / W * 2 - 1, x1 = (q.x + q.w) / W * 2 - 1;
				float y0 = 1 - q.y / H * 2, y1 = 1 - (q.y + q.h) / H * 2;
				Vtx quad[6] = { { x0, y0, 0, 0 }, { x1, y0, 1, 0 }, { x0, y1, 0, 1 }, { x1, y0, 1, 0 }, { x1, y1, 1, 1 }, { x0, y1, 0, 1 } };
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

	// SM64's layout idea, moved clear of Skyrim's compass: the power meter top-left, coins and stars beside it
	std::vector<Quad> Layout(const State& a_s, float a_screenH)
	{
		std::vector<Quad> q;
		const float s = a_screenH / 240.0f * 0.75f;   // SM64 pixels -> screen
		const float x0 = 24 * s / 0.75f * 0.5f, y0 = 20 * s / 0.75f * 0.5f;
		auto add = [&](const std::string& a_name, float x, float y, float w, float h) {
			if (auto im = Img(a_name)) q.push_back({ im, x0 + x * s, y0 + y * s, w * s, h * s });
		};
		add("meter_left", 0, 0, 32, 64);
		add("meter_right", 32, 0, 32, 64);
		if (a_s.wedges > 0) add("pie_" + std::to_string(std::clamp(a_s.wedges, 1, 8)), 16, 16, 32, 32);

		auto counter = [&](const char* a_icon, int a_value, float y) {
			add(a_icon, 72, y, 16, 16);
			add("times", 88, y, 16, 16);
			auto digits = std::to_string(std::clamp(a_value, 0, 9999));
			for (std::size_t i = 0; i < digits.size(); i++) add(std::string("digit_") + digits[i], 104 + 12.0f * i, y, 16, 16);
		};
		counter("coin", a_s.coins, 8);
		counter("star", a_s.stars, 30);
		return q;
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
				Draw(a_swap, Layout(st, static_cast<float>(d.BufferDesc.Height)));
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
	logger::info("hud: {} graphics from the ROM", s_images.size());
	return !s_images.empty();
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
}

void Hud::Tick(RE::PlayerCharacter* a_player, bool a_marioActive, int a_wedges, float a_delta)
{
	static float s_countTimer = 0.0f, s_starTimer = 0.0f, s_telTimer = 0.0f;
	static int   s_coins = -1, s_stars = 0;

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

	auto ui = RE::UI::GetSingleton();
	const bool menus = ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
	{
		std::scoped_lock l(s_lock);
		s_state.show = a_marioActive && !menus && !s_images.empty();
		s_state.wedges = a_wedges;
		s_state.coins = std::max(0, s_coins);
		s_state.stars = s_stars;
	}
	if (Config::telemetry && a_marioActive && (s_telTimer += a_delta) >= 1.0f) {
		s_telTimer = 0.0f;
		logger::info("HUD shown={} wedges={} coins={} stars={} gpu={}", s_state.show ? 1 : 0, a_wedges, s_state.coins, s_stars, s_gpuReady ? 1 : 0);
	}
}
