// Plugin independente: nao altera o codigo do motor.
// Requer Source Build porque inclui headers de Renderer/Private (FViewInfo).
using System.IO;
using UnrealBuildTool;

public class SoftwareRTReflections : ModuleRules
{
	public SoftwareRTReflections(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RenderCore",
			"RHI",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",   // IPluginManager (mapeamento da pasta de shaders)
			"Renderer",   // headers privados do renderer (FViewInfo, PrevViewInfo)
		});

		// FViewInfo / FScene vivem em Renderer/Private. So os membros definidos em header sao usaveis
		// a partir de um plugin; simbolos nao exportados (sem RENDERER_API) nao fazem link.
		string EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
		PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/Renderer/Private"));
		PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/Renderer/Public"));
	}
}
