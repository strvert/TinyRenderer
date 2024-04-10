using UnrealBuildTool;

public class TinyRenderer : ModuleRules
{
	public TinyRenderer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.AddRange(
			new string[]
			{
				System.IO.Path.Combine(GetModuleDirectory("Renderer"), "Private"),
			}
		);


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Engine",
				"RHI",
				"RenderCore",
				"Renderer",
				"Projects",
			}
		);
	}
}