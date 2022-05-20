package hw_gralloc

import (
    "android/soong/android"
    "android/soong/cc"
    "fmt"
)

func init() {
    android.RegisterModuleType("hw_gralloc_go_defaults",gralloc_DefaultsFactory)
}

func gralloc_DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, hw_gralloc_aml_Defaults)
    return module
}

func hw_gralloc_aml_Defaults(ctx android.LoadHookContext) {
    type propsE struct {
        Shared_libs  []string
    }
    p := &propsE{}
    PlatformVndkVersion := ctx.DeviceConfig().PlatformVndkVersion()
    fmt.Println("PlatformVndkVersion:", PlatformVndkVersion)
    //For Andriod T, before freeze API PlatformVndkVersion return string like "Tiramisu", 
    //after freeze API it will be changed to be numbers like normal android release "32"
    if len(PlatformVndkVersion) > 2 {
         p.Shared_libs = append(p.Shared_libs, "arm.graphics-V1-ndk")
     } else {
         p.Shared_libs = append(p.Shared_libs, "arm.graphics-V1-ndk_platform")
     }

    ctx.AppendProperties(p)
}
