package hw_gralloc

import (
	"android/soong/android"
	"android/soong/cc"
	//"fmt"
)

func init() {
	android.RegisterModuleType("hw_gralloc_go_defaults", gralloc_DefaultsFactory)
}

func gralloc_DefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, hw_gralloc_aml_Defaults)
	return module
}

func hw_gralloc_aml_Defaults(ctx android.LoadHookContext) {
	type propsE struct {
		Shared_libs []string
	}
	p := &propsE{}

	p.Shared_libs = append(p.Shared_libs, "arm.graphics-V4-ndk")
	ctx.AppendProperties(p)
}
