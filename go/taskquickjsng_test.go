package taskquickjsng

import (
	"context"
	"testing"

	extism "github.com/extism/go-sdk"
)

func BenchmarkLoad(b *testing.B) {
	manifest := extism.Manifest{
		Wasm: []extism.Wasm{
			extism.WasmFile{
				Path: "../build/qjs.wasm",
			},
		},
	}

	ctx := context.Background()
	config := extism.PluginConfig{
		EnableWasi: true,
	}
	plugin, err := extism.NewPlugin(ctx, manifest, config, []extism.HostFunction{})

	if err != nil {
		b.Error(err)
	}

	defer plugin.Close(ctx)

	_, _, err = plugin.Call("warmup", nil)

	if err != nil {
		b.Error(err)
	}
}
