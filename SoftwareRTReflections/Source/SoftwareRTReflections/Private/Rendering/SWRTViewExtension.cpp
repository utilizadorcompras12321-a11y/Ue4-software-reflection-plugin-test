#include "SWRTViewExtension.h"
#include "SWRTCVars.h"
#include "SWRTCompat.h"

DEFINE_LOG_CATEGORY_STATIC(LogSWRTRender, Log, All);

// =====================================================================================================
// Shaders (Global Shaders, SM5)
// =====================================================================================================

// Compute: 1 raio por pixel de RT. M1: contra um plano analitico Z = PlaneZ (+ ceu como miss).
class FSWRTSpikeCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSWRTSpikeCS);
	SHADER_USE_PARAMETER_STRUCT(FSWRTSpikeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
		SHADER_PARAMETER(FIntPoint, RTExtent)
		SHADER_PARAMETER(float, MaxDistance)
		SHADER_PARAMETER(float, MaxRoughness)
		SHADER_PARAMETER(float, PlaneZ)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutReflection)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SWRT_THREADGROUP_SIZE"), 8);
	}
};
IMPLEMENT_GLOBAL_SHADER(FSWRTSpikeCS, "/Plugin/SoftwareRTReflections/Private/SWRTSpike.usf", "MainCS", SF_Compute);

// Pixel: le o GBuffer, calcula o peso de Fresnel (EnvBRDF) e compoe o reflexo no SceneColor.
class FSWRTCompositePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSWRTCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FSWRTCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ReflectionTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ReflectionSampler)
		SHADER_PARAMETER(float, MaxRoughness)
		SHADER_PARAMETER(int32, DebugMode)
		SHADER_PARAMETER(int32, CompositeMode)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
IMPLEMENT_GLOBAL_SHADER(FSWRTCompositePS, "/Plugin/SoftwareRTReflections/Private/SWRTComposite.usf", "MainPS", SF_Pixel);

// =====================================================================================================
// View extension
// =====================================================================================================

FSWRTViewExtension::FSWRTViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

bool FSWRTViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	// Chamado na game thread. O CVar e RenderThreadSafe, por isso e seguro ler aqui.
	return true; // o gate real e feito na render thread (GetSettingsRenderThread)
}

void FSWRTViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	const FSWRTSettings Settings = SWRT::GetSettingsRenderThread();

	// Diagnostico: regista (so quando muda) se o hook e chamado e porque razao sai cedo.
	{
		static int32 LastState = -1;
		const bool bSkipView = SWRT::ShouldSkipView(View);
		const bool bLowFL    = View.GetFeatureLevel() < ERHIFeatureLevel::SM5;
		const int32 State    = (Settings.bEnabled ? 1 : 0) | (bSkipView ? 2 : 0) | (bLowFL ? 4 : 0) | (Settings.Debug << 4);
		if (State != LastState)
		{
			LastState = State;
			UE_LOG(LogSWRTRender, Warning,
				TEXT("[SWRT] PrePostProcessPass chamado: enabled=%d skipView=%d featureLevelBaixo=%d debug=%d compositeMode=%d"),
				Settings.bEnabled, bSkipView, bLowFL, Settings.Debug, Settings.CompositeMode);
		}
	}

	if (!Settings.bEnabled || SWRT::ShouldSkipView(View) || View.GetFeatureLevel() < ERHIFeatureLevel::SM5)
	{
		return;
	}

	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);
	const FIntRect ViewRect = SWRT::GetInternalViewRect(View);
	const FIntPoint InternalSize = ViewRect.Size();

	// Resolucao relativa a resolucao INTERNA (apos Screen Percentage), nunca a do monitor.
	const FIntPoint RTExtent = SWRT::ComputeScaledExtent(InternalSize, Settings.ResolutionScale);

	{
		static FIntPoint LastExtent(-1, -1);
		if (LastExtent != RTExtent)
		{
			LastExtent = RTExtent;
			UE_LOG(LogSWRTRender, Warning, TEXT("[SWRT] ViewRect interno %dx%d -> RT %dx%d (escala %.2f)"),
				InternalSize.X, InternalSize.Y, RTExtent.X, RTExtent.Y, Settings.ResolutionScale);
		}
	}

	RDG_EVENT_SCOPE(GraphBuilder, "SoftwareRTReflections");

	FRDGTextureDesc ReflDesc = FRDGTextureDesc::Create2D(
		RTExtent, PF_FloatRGBA, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef ReflectionTexture = GraphBuilder.CreateTexture(ReflDesc, TEXT("SWRT.Reflection"));

	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	const auto SceneTextures = SWRT::GetSceneTexturesUB(Inputs);

	// ---- Passo 1: raios (compute) --------------------------------------------------------------
	{
		FSWRTSpikeCS::FParameters* P = GraphBuilder.AllocParameters<FSWRTSpikeCS::FParameters>();
		P->View           = ViewInfo.ViewUniformBuffer;
		P->SceneTextures  = SceneTextures;
		P->RTExtent       = RTExtent;
		P->MaxDistance    = Settings.MaxDistance;
		P->MaxRoughness   = Settings.MaxRoughness;
		P->PlaneZ         = Settings.PlaneZ;
		P->OutReflection  = GraphBuilder.CreateUAV(ReflectionTexture);

		TShaderMapRef<FSWRTSpikeCS> CS(ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder, RDG_EVENT_NAME("SWRT Spike Trace %dx%d", RTExtent.X, RTExtent.Y),
			CS, P, FComputeShaderUtils::GetGroupCount(RTExtent, 8));
	}

	// ---- Passo 2: composicao no SceneColor (HDR, antes do tonemap) ------------------------------
	{
		FSWRTCompositePS::FParameters* P = GraphBuilder.AllocParameters<FSWRTCompositePS::FParameters>();
		P->View              = ViewInfo.ViewUniformBuffer;
		P->SceneTextures     = SceneTextures;
		P->ReflectionTexture = ReflectionTexture;
		P->ReflectionSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		P->MaxRoughness      = Settings.MaxRoughness;
		P->DebugMode         = Settings.Debug;
		P->CompositeMode     = Settings.CompositeMode;
		P->RenderTargets[0]  = FRenderTargetBinding(SWRT::GetSceneColor(Inputs), ERenderTargetLoadAction::ELoad);

		const bool bAdditive = (Settings.CompositeMode == 0) && (Settings.Debug == 0);
		FRHIBlendState* Blend = bAdditive
			// 0: SceneColor += rgb. NOTA: soma ao reflexo que o motor ja calculou (ver README, limitacao A).
			? TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One>::GetRHI()
			// 1 (ou qualquer Debug != 0): SceneColor = lerp(SceneColor, rgb, alpha). Substitui onde ha confianca.
			: TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();

		TShaderMapRef<FSWRTCompositePS> PS(ShaderMap);
		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder, ShaderMap, RDG_EVENT_NAME("SWRT Composite"), PS, P, ViewRect, Blend);
	}
}
