#pragma once

#include "Modules/ModuleManager.h"

class FSWRTViewExtension;

class FSoftwareRTReflectionsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	// O modulo carrega em PostConfigInit (necessario para registar Global Shaders), mas nessa fase o
	// GEngine ainda nao existe e o registo de view extensions vive la. Por isso a extensao e criada
	// em OnPostEngineInit.
	void CreateViewExtension();

	TSharedPtr<FSWRTViewExtension, ESPMode::ThreadSafe> ViewExtension;
	FDelegateHandle PostEngineInitHandle;
};
