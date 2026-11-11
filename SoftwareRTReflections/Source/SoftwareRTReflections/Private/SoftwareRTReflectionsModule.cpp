#include "SoftwareRTReflections.h"
#include "Rendering/SWRTViewExtension.h"

#include "Engine/Engine.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "SceneViewExtension.h"
#include "ShaderCore.h" // AddShaderSourceDirectoryMapping

DEFINE_LOG_CATEGORY_STATIC(LogSWRT, Log, All);

void FSoftwareRTReflectionsModule::StartupModule()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SoftwareRTReflections"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogSWRT, Error, TEXT("Plugin 'SoftwareRTReflections' nao encontrado; shaders nao serao mapeados."));
		return;
	}
	const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/SoftwareRTReflections"), ShaderDir);
	UE_LOG(LogSWRT, Log, TEXT("Shaders mapeados: %s"), *ShaderDir);

	if (!FApp::CanEverRender())
	{
		UE_LOG(LogSWRT, Log, TEXT("Sem renderizacao (commandlet/servidor): view extension nao criada."));
		return;
	}

	if (GEngine)
	{
		CreateViewExtension();
	}
	else
	{
		PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FSoftwareRTReflectionsModule::CreateViewExtension);
		UE_LOG(LogSWRT, Log, TEXT("GEngine ainda nao existe; view extension sera criada em OnPostEngineInit."));
	}
}

void FSoftwareRTReflectionsModule::CreateViewExtension()
{
	if (ViewExtension.IsValid())
	{
		return;
	}
	if (!GEngine)
	{
		UE_LOG(LogSWRT, Error, TEXT("GEngine nulo em CreateViewExtension: a extensao NAO sera registada."));
		return;
	}
	ViewExtension = FSceneViewExtensions::NewExtension<FSWRTViewExtension>();
	UE_LOG(LogSWRT, Warning, TEXT("[SWRT] View extension criada e registada."));
}

void FSoftwareRTReflectionsModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
	ViewExtension.Reset();
}

IMPLEMENT_MODULE(FSoftwareRTReflectionsModule, SoftwareRTReflections)
