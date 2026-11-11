#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

// Milestone 1: spike de composicao.
// Lanca 1 raio por pixel de RT contra um plano analitico (sem BVH ainda) e compoe o resultado
// em HDR antes do post process, para validar:
//   (a) integracao pelo plugin sem alterar o motor,
//   (b) resolucao relativa a resolucao interna,
//   (c) o problema de reflexo duplicado com SSR/captures (ver README).
class FSWRTViewExtension : public FSceneViewExtensionBase
{
public:
	FSWRTViewExtension(const FAutoRegister& AutoRegister);

	// --- obrigatorios (puros) em ISceneViewExtension ---
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override {}

	// --- o que usamos ---
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs) override;

	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
};
