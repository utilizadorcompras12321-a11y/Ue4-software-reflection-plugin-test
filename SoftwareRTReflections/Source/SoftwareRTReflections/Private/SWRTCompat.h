#pragma once

// =====================================================================================================
// SWRTCompat.h - TODAS as dependencias de API do motor que NAO foram confirmadas contra o source 4.27.2
// ficam aqui. Cada ponto marcado VERIFY[n] esta listado no README com o comando para o confirmar.
// Se algo nao compilar, o erro deve estar aqui e so aqui.
//
// CONFIRMADO no SceneViewExtension.h 4.27.2 enviado:
//   - PrePostProcessPass_RenderThread(FRDGBuilder&, const FSceneView&, const FPostProcessingInputs&)
//   - IsActiveThisFrame_Internal(const FSceneViewExtensionContext&) const
//   - FSceneViewExtensions::NewExtension<T>() devolve TSharedRef<T, ESPMode::ThreadSafe>
// CONFIRMADO no SceneRendering.h: FViewInfo::ViewRect, FViewInfo::PrevViewInfo
// =====================================================================================================

#include "CoreMinimal.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

// VERIFY[1]: FViewInfo (Renderer/Private/SceneRendering.h)
#include "SceneRendering.h"

// VERIFY[2]: FPostProcessingInputs. No 4.27 esta (a confirmar) em Renderer/Private/PostProcess/PostProcessing.h.
//            Se o include falhar: Select-String -Path Engine\Source\Runtime\Renderer -Pattern "struct FPostProcessingInputs" -Recurse
#include "PostProcess/PostProcessing.h"

// VERIFY[3]: FSceneTextureUniformParameters (normalmente incluido por SceneRendering.h / SceneRenderTargetParameters.h)
// VERIFY[4]: FPixelShaderUtils::AddFullscreenPass (RenderCore/Public/PixelShaderUtils.h)
#include "PixelShaderUtils.h"

namespace SWRT
{
	// Resolucao interna efetivamente renderizada (ja com Screen Percentage). CONFIRMADO: FViewInfo::ViewRect.
	inline FIntRect GetInternalViewRect(const FSceneView& View)
	{
		return static_cast<const FViewInfo&>(View).ViewRect;
	}

	// VERIFY[5]: Inputs.SceneTextures e um TRDGUniformBufferRef<FSceneTextureUniformParameters> e
	//            (*Inputs.SceneTextures)->SceneColorTexture e um FRDGTextureRef.
	inline TRDGUniformBufferRef<FSceneTextureUniformParameters> GetSceneTexturesUB(const FPostProcessingInputs& Inputs)
	{
		return Inputs.SceneTextures;
	}

	inline FRDGTextureRef GetSceneColor(const FPostProcessingInputs& Inputs)
	{
		return (*Inputs.SceneTextures)->SceneColorTexture;
	}

	// VERIFY[6]: membros de FSceneView usados para ignorar views que nao sao a camara principal.
	inline bool ShouldSkipView(const FSceneView& View)
	{
		return View.bIsSceneCapture || View.bIsReflectionCapture || View.bIsPlanarReflection;
	}
}
