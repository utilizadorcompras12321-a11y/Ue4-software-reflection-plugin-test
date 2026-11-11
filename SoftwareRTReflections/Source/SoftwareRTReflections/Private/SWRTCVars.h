#pragma once

#include "CoreMinimal.h"

// Snapshot dos CVars lido na render thread uma vez por view.
struct FSWRTSettings
{
	bool  bEnabled          = false;
	float ResolutionScale   = 0.5f;   // relativo a resolucao INTERNA (ViewRect), nao ao monitor
	float DenoiseScale      = 1.0f;   // relativo a resolucao de RT (M2)
	float CompositeScale    = 1.0f;   // relativo a resolucao interna   (M2)
	int32 Samples           = 1;
	int32 MaxRays           = 0;      // 0 = sem limite (M2)
	float MaxDistance       = 5000.f; // cm
	int32 BvhMode           = 2;      // 0 binario, 1 BVH4, 2 BVH4 comprimido
	int32 BvhCompression    = 1;
	int32 Denoiser          = 1;      // M3
	int32 Temporal          = 0;      // desligado por defeito
	int32 Lod               = 1;      // M2
	int32 Culling           = 1;      // M2
	float MaxRoughness      = 0.5f;   // acima disto o SWRT nao atua (SSR/captures)

	// Spike M1
	int32 CompositeMode     = 0;      // 0 = aditivo, 1 = lerp (peso = confianca)
	int32 Debug             = 0;      // 0 normal, 1 so reflexo, 2 mascara de rugosidade
	float PlaneZ            = 0.f;    // altura (cm, mundo) do plano de teste
};

namespace SWRT
{
	// Ler apenas na render thread.
	FSWRTSettings GetSettingsRenderThread();

	// W_RT = align8(W_Internal * Scale), com minimo de 8.
	FIntPoint ComputeScaledExtent(const FIntPoint& InternalSize, float Scale);
}
