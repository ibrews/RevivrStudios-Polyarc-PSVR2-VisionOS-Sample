using UnrealBuildTool;
public class XrsimAgent : ModuleRules
{
    public XrsimAgent(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "UnrealEd", "HeadMountedDisplay", "XRBase", "InputCore", "Json"
        });
    }
}
