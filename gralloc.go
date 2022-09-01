package hw_gralloc

import (
    "android/soong/android"
    "android/soong/cc"
    "fmt"
    "strconv"
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
    IntPlatformVndkVersion,err := strconv.Atoi(PlatformVndkVersion)
    // For An Android letter, before freeze API PlatformVndkVersion return code name, like
    // "Tiramisu", after freeze API it has been changed to number.
    if err != nil {
        fmt.Printf("%v fail to convert", IntPlatformVndkVersion)
        p.Shared_libs = append(p.Shared_libs, "arm.graphics-V1-ndk")
    } else {
        if IntPlatformVndkVersion > 32 {
            p.Shared_libs = append(p.Shared_libs, "arm.graphics-V3-ndk")
        } else {
            p.Shared_libs = append(p.Shared_libs, "arm.graphics-V1-ndk_platform")
        }
    }
    ctx.AppendProperties(p)
}
