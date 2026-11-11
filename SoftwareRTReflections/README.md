# SoftwareRTReflections — Milestone 1 (UE 4.27.2, DX11, SM5)

Plugin independente (sem alterar o motor) para reflexos por ray tracing em software.
**Este Milestone NÃO tem ainda BVH na GPU nem geometria.** Contém:

| Parte | Estado |
|---|---|
| `.uplugin`, `.Build.cs`, módulo, mapeamento de shaders | Escrito. **Não compilado** (não tenho o motor aqui). |
| CVars (todas as do spec + 3 de spike) | Escrito. Não compilado. |
| View extension + composição (spike) | Escrito contra assinaturas **confirmadas** em `SceneViewExtension.h`. Resto marcado `VERIFY`. Não compilado. |
| Shaders `SWRTSpike.usf`, `SWRTComposite.usf` | Escritos. Não compilados. |
| BVH builder (binário, BVH4, BVH4 comprimido) | **Compilado e testado na CPU** (`Tools/bvh_test.cpp`). |
| `SWRTBvhTraversal.ush` (travessia GPU) | Escrito, **não compilado**. Layout validado por transliteração em C++. |

## Instalar

1. Copia a pasta `SoftwareRTReflections` para `<Projeto>/Plugins/` (ou `Engine/Plugins/Runtime/`).
2. Gera os ficheiros do projeto e compila em **Development Editor, Win64** (Source Build).
3. Abre o projeto. No log deve aparecer: `SoftwareRTReflections iniciado. Shaders: ...`.
4. Os shaders compilam na primeira utilização (pode demorar).

## Testar o spike (o que este milestone serve para validar)

Numa cena com um chão polido/metálico (Roughness baixa), na consola:

```
r.SoftwareRT.Reflection 1
r.SoftwareRT.Reflection.Spike.PlaneZ 0        <- altura do plano de teste (cm). Um CHÃO reflete para cima: põe o plano ACIMA do chão (ex.: chão em Z=0 -> PlaneZ 300) para o veres refletido
r.SoftwareRT.Reflection.Spike.Debug 2         <- vê a máscara de rugosidade (confirma que o GBuffer é lido)
r.SoftwareRT.Reflection.Spike.Debug 1         <- vê só o reflexo (céu + plano axadrezado)
r.SoftwareRT.Reflection.Spike.Debug 0         <- composição normal
r.SoftwareRT.Reflection.ResolutionScale 0.5   <- muda e confirma que a resolução acompanha o Screen Percentage
```

**A experiência principal (limitação A):** com `Debug 0`, `CompositeMode 0` (aditivo) e o SSR do motor ligado, vês o reflexo duplicado.
Repete com `r.SSR.Quality 0`. Compara com `CompositeMode 1` (mistura). O que observares decide como se faz a composição final.
Diz-me o que vês: é a decisão de arquitetura mais importante que falta validar.

## O que tens de confirmar no teu source (`VERIFY`)

Se algo não compilar, o erro deve estar em `Private/SWRTCompat.h` ou nos shaders. Comandos (PowerShell, na pasta que contém `Engine\`):

| # | O quê | Comando |
|---|---|---|
| 1 | `FViewInfo` em `SceneRendering.h` | já confirmado (ficheiro enviado) |
| 2 | Onde está `FPostProcessingInputs` | `Select-String -Path "Engine\Source\Runtime\Renderer" -Pattern "struct FPostProcessingInputs" -Recurse` |
| 3 | `FSceneTextureUniformParameters` | `Select-String -Path "Engine\Source\Runtime" -Pattern "FSceneTextureUniformParameters" -Recurse -List` |
| 4 | `FPixelShaderUtils::AddFullscreenPass` | `Select-String -Path "Engine\Source\Runtime\RenderCore\Public\PixelShaderUtils.h" -Pattern "AddFullscreenPass"` |
| 5 | `SceneColorTexture` em `FSceneTextureUniformParameters` | `Select-String -Path "Engine\Source\Runtime" -Pattern "SceneColorTexture" -Recurse -List` (ver a definição da struct) |
| 6 | `bIsSceneCapture`, `bIsReflectionCapture`, `bIsPlanarReflection` | `Select-String -Path "Engine\Source\Runtime\Engine\Public\SceneView.h" -Pattern "bIsSceneCapture\|bIsReflectionCapture\|bIsPlanarReflection"` |
| 7 | `View.BufferSizeAndInvSize`, `View.ViewRectMin` | `Select-String -Path "Engine\Shaders\Private\*.ush" -Pattern "BufferSizeAndInvSize\|ViewRectMin" -List` |
| 8 | `LookupDeviceZ` | `Select-String -Path "Engine\Shaders\Private\*.ush" -Pattern "LookupDeviceZ" -List` |
| 9 | `FRDGTextureDesc::Create2D(Extent, Format, Clear, Flags)` | `Select-String -Path "Engine\Source\Runtime\RenderCore\Public\RenderGraphResources.h" -Pattern "Create2D"` |

Cola-me os erros do compilador ou os resultados e eu corrijo.

## Testar a BVH (sem o Unreal)

```
cd Tools
g++ -O2 -std=c++17 -I../Source/SoftwareRTReflections/Private/BVH bvh_test.cpp -o bvh_test && ./bvh_test
```

Verifica: os 3 formatos dão o mesmo hit que a força bruta; a compressão a 8 bits é conservadora
(a caixa descomprimida contém sempre a original); cada triângulo aparece uma vez nas folhas; e o decode "estilo HLSL" dá o mesmo que o do C++.

### Resultados MEDIDOS (CPU, o meu sandbox; só servem para comparar formatos entre si, não para prever ms de GPU)

| Cena | Formato | Nós | Bytes | Nós/ray | Testes tri/ray |
|---|---|---|---|---|---|
| 50 k tris | binário | 33 751 | 1,08 MB | 91,2 | 16,4 |
| | BVH4 | 8 123 | 1,04 MB | 18,6 | 12,3 |
| | BVH4 comprimido | 8 123 | 0,52 MB | 19,0 | 12,5 |
| 300 k tris | binário | 203 063 | 6,50 MB | 141,1 | 32,2 |
| | BVH4 | 48 449 | 6,20 MB | 22,6 | 16,5 |
| | BVH4 comprimido | 48 449 | 3,10 MB | 23,0 | 16,8 |

Leitura honesta:
- A compressão **corta a memória para metade** e visita ~2 % mais nós (caixas ligeiramente mais largas), com resultados idênticos.
- "Nós/ray" **não é comparável** entre binário e BVH4 (um nó BVH4 testa 4 caixas). E o binário aqui percorre sem ordenar filhos por distância, o que o penaliza nos testes de triângulo. A comparação justa do binário só se faz **em GPU**, com o CVar `r.SoftwareRT.Reflection.BVH`. Ainda não há medição de GPU.
- Cenas de teste: nuvens de triângulos aleatórios em clusters, não geometria real.

## Notas de testes reais

- **Erro `unbound parameters: PreIntegratedGF`:** corrigido no `SWRTComposite.usf` (troca de `EnvBRDF` por `EnvBRDFApprox`). Se mais tarde quiseres a LUT exata, tens de ligar `PreIntegratedGF` e `PreIntegratedGFSampler` na struct de parâmetros do `FSWRTCompositePS`.
- O stack trace do utilizador mostrava `D3D12RHI`. O alvo deste plugin é **DX11/SM5**: confirma que o editor corre em DX11 (Project Settings > Platforms > Windows > Default RHI = DirectX 11, ou arranca com `-dx11`).

- **"Nada aparece" (nem em debug):** a view extension era criada em `PostConfigInit`, antes de o `GEngine` existir. O registo de extensões vive no `GEngine` (ver `FSceneViewExtensions::KnownExtensions` no header), por isso nunca ficava ativa. Agora é criada em `OnPostEngineInit`. O log passa a mostrar `[SWRT] View extension criada e registada.`, `[SWRT] PrePostProcessPass chamado: ...` e `[SWRT] ViewRect interno ... -> RT ...`.
- **`Spike.Debug 3`**: magenta sólido, sem ler GBuffer nem textura. Se aparecer, o passe chega ao ecrã e o problema (se houver) está na leitura do GBuffer/depth.

## Limitações conhecidas (explícitas)

1. **Substituir o reflexo do motor não é possível pelo plugin** (entra no passe de iluminação). Compõe-se depois, em HDR, antes do tonemap. O spike existe para medir isto.
2. **Sem material real no hit** (sem bindless em DX11 e sem como registar um mesh pass novo sem alterar o enum do motor). Plano: atlas de texturas baked + luz aproximada, e cor do frame anterior via `PrevViewInfo.ScreenSpaceRayTracingInput`.
3. **Skeletal:** o acesso é viável (`FGPUSkinCache::GetCachedGeometry` é `ENGINE_API`), mas falta confirmar como obter o `FGPUSkinCache*` da cena (preciso de `ScenePrivate.h`/`SceneInterface.h`).
4. **Índices das Static Meshes:** não confirmei SRV do index buffer nem a existência de dados de CPU em build empacotada.
5. A pilha da travessia em `groupshared` tem 32 entradas por thread; se estourar, o ramo é ignorado (raios podem falhar geometria). Não deve acontecer com BVH4 e ~10 níveis, mas há que verificar em cenas grandes.

## Próximos milestones

- **M2:** recolha de Static Meshes, BLAS por mesh (asset gerado no editor), TLAS, upload, ligação de `SWRTBvhTraversal.ush`, LOD por footprint, culling por grelha de superfícies refletivas.
- **M3:** denoiser espacial à-trous guiado por normal/depth/distância do hit/variância, blue noise, upsampling guiado.
- **M4:** skeletal (Skin Cache), depois efeitos (sprites orientados ao raio para partículas).

## Estrutura

```
SoftwareRTReflections/
  SoftwareRTReflections.uplugin
  README.md
  Source/SoftwareRTReflections/
    SoftwareRTReflections.Build.cs
    Public/SoftwareRTReflections.h
    Private/
      SoftwareRTReflectionsModule.cpp
      SWRTCVars.h / .cpp
      SWRTCompat.h                 <- todas as APIs por confirmar
      BVH/SWRTBvh.h                <- builder (testado)
      Rendering/SWRTViewExtension.h / .cpp
  Shaders/Private/
    SWRTSpike.usf  SWRTComposite.usf  SWRTBvhTraversal.ush
  Tools/bvh_test.cpp
```
