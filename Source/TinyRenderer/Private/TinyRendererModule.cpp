#include "TinyRendererModule.h"

#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FTinyRendererModule"

void FTinyRendererModule::StartupModule()
{
	IPluginManager& PluginManager = IPluginManager::Get();
	const FString PluginShaderDir = PluginManager.FindPlugin(TEXT("TinyRenderer"))->GetBaseDir() / TEXT("Shaders");
	AddShaderSourceDirectoryMapping(TEXT("/TinyRenderer"), PluginShaderDir);
}

void FTinyRendererModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTinyRendererModule, TinyRenderer)