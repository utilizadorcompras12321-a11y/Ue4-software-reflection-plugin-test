#include "SWRTCVars.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarEnable(
	TEXT("r.SoftwareRT.Reflection"), 0,
	TEXT("Liga os reflexos por ray tracing em software. 0 = desligado (default), 1 = ligado."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarResolutionScale(
	TEXT("r.SoftwareRT.Reflection.ResolutionScale"), 0.5f,
	TEXT("Escala da resolucao do ray tracing, relativa a resolucao interna (apos Screen Percentage). W = align8(W_int * escala)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarDenoiseScale(
	TEXT("r.SoftwareRT.Reflection.DenoiseScale"), 1.0f,
	TEXT("Escala da resolucao do denoiser, relativa a resolucao de RT. (Milestone 3)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarCompositeScale(
	TEXT("r.SoftwareRT.Reflection.CompositeScale"), 1.0f,
	TEXT("Escala da resolucao da composicao final, relativa a resolucao interna. (Milestone 2)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSamples(
	TEXT("r.SoftwareRT.Reflection.Samples"), 1,
	TEXT("Amostras por pixel para rugosidade media. Espelhos usam sempre 1 raio deterministico."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMaxRays(
	TEXT("r.SoftwareRT.Reflection.MaxRays"), 0,
	TEXT("Teto global de raios por frame (0 = sem teto). (Milestone 2)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarMaxDistance(
	TEXT("r.SoftwareRT.Reflection.MaxDistance"), 5000.f,
	TEXT("Distancia maxima do raio em cm."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarBvh(
	TEXT("r.SoftwareRT.Reflection.BVH"), 2,
	TEXT("Formato da BVH: 0 = binario, 1 = BVH4, 2 = BVH4 comprimido. (Milestone 2)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarBvhCompression(
	TEXT("r.SoftwareRT.Reflection.BVH.Compression"), 1,
	TEXT("Compressao das caixas dos filhos (8 bit) quando BVH = 2. (Milestone 2)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDenoiser(
	TEXT("r.SoftwareRT.Reflection.Denoiser"), 1,
	TEXT("Denoiser espacial (a-trous guiado por normal, depth e variancia). (Milestone 3)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarTemporal(
	TEXT("r.SoftwareRT.Reflection.Temporal"), 0,
	TEXT("Acumulacao temporal opcional. Desligada por defeito."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarLod(
	TEXT("r.SoftwareRT.Reflection.LOD"), 1,
	TEXT("Selecao de LOD pelo footprint do raio. 0 = LOD0 sempre. (Milestone 2)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarCulling(
	TEXT("r.SoftwareRT.Reflection.Culling"), 1,
	TEXT("Culling pela grelha de superficies refletivas visiveis (nao pela camera principal). (Milestone 2)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarMaxRoughness(
	TEXT("r.SoftwareRT.Reflection.MaxRoughness"), 0.5f,
	TEXT("Rugosidade maxima tratada pelo SWRT. Acima disto fica o SSR/captures do motor."),
	ECVF_RenderThreadSafe);

// ---- Spike do Milestone 1 ------------------------------------------------------------------
static TAutoConsoleVariable<int32> CVarCompositeMode(
	TEXT("r.SoftwareRT.Reflection.Spike.CompositeMode"), 0,
	TEXT("Spike: 0 = soma ao SceneColor (pode duplicar o reflexo do motor), 1 = mistura pela confianca."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarDebug(
	TEXT("r.SoftwareRT.Reflection.Spike.Debug"), 0,
	TEXT("Spike: 0 = normal, 1 = so o reflexo, 2 = mascara de rugosidade, 3 = magenta solido (testa se o passe chega ao ecra)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarPlaneZ(
	TEXT("r.SoftwareRT.Reflection.Spike.PlaneZ"), 0.f,
	TEXT("Spike: altura Z (cm, mundo) do plano analitico contra o qual os raios sao lancados."),
	ECVF_RenderThreadSafe);

namespace SWRT
{
	FSWRTSettings GetSettingsRenderThread()
	{
		FSWRTSettings S;
		S.bEnabled        = CVarEnable.GetValueOnRenderThread() != 0;
		S.ResolutionScale = FMath::Clamp(CVarResolutionScale.GetValueOnRenderThread(), 0.05f, 1.0f);
		S.DenoiseScale    = FMath::Clamp(CVarDenoiseScale.GetValueOnRenderThread(), 0.05f, 1.0f);
		S.CompositeScale  = FMath::Clamp(CVarCompositeScale.GetValueOnRenderThread(), 0.05f, 1.0f);
		S.Samples         = FMath::Max(1, CVarSamples.GetValueOnRenderThread());
		S.MaxRays         = FMath::Max(0, CVarMaxRays.GetValueOnRenderThread());
		S.MaxDistance     = FMath::Max(1.f, CVarMaxDistance.GetValueOnRenderThread());
		S.BvhMode         = FMath::Clamp(CVarBvh.GetValueOnRenderThread(), 0, 2);
		S.BvhCompression  = CVarBvhCompression.GetValueOnRenderThread();
		S.Denoiser        = CVarDenoiser.GetValueOnRenderThread();
		S.Temporal        = CVarTemporal.GetValueOnRenderThread();
		S.Lod             = CVarLod.GetValueOnRenderThread();
		S.Culling         = CVarCulling.GetValueOnRenderThread();
		S.MaxRoughness    = FMath::Clamp(CVarMaxRoughness.GetValueOnRenderThread(), 0.f, 1.f);
		S.CompositeMode   = CVarCompositeMode.GetValueOnRenderThread();
		S.Debug           = CVarDebug.GetValueOnRenderThread();
		S.PlaneZ          = CVarPlaneZ.GetValueOnRenderThread();
		return S;
	}

	FIntPoint ComputeScaledExtent(const FIntPoint& InternalSize, float Scale)
	{
		auto Align8 = [](int32 V) { return FMath::Max(8, (V + 7) & ~7); };
		return FIntPoint(
			Align8(FMath::CeilToInt(InternalSize.X * Scale)),
			Align8(FMath::CeilToInt(InternalSize.Y * Scale)));
	}
}
