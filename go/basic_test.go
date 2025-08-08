package tqn_test

import (
	"bytes"
	"context"
	"strings"
	"testing"

	extism "github.com/extism/go-sdk"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	"github.com/tetratelabs/wazero"
)

func TestCivetCompile(t *testing.T) {
	manifest := extism.Manifest{
		Wasm:   []extism.Wasm{extism.WasmFile{Path: "../build/qjs.wasm"}},
		Config: map[string]string{},
	}

	ctx := context.Background()
	config := extism.PluginConfig{
		EnableWasi:   true,
		ModuleConfig: wazero.NewModuleConfig(),
	}
	plugin, err := extism.NewPlugin(ctx, manifest, config, []extism.HostFunction{})
	require.NoError(t, err)
	defer plugin.Close(ctx)

	_, _, err = plugin.Call("warmup", nil)
	require.NoError(t, err)

	var out []byte
	_, out, err = plugin.Call("civet", []byte("[1,2,3] |> .map & * 2 |> print"))
	require.NoError(t, err)
	assert.Equal(t, "print([1,2,3].map($ => $ * 2))", string(out))
}

func TestEnv(t *testing.T) {
	manifest := extism.Manifest{
		Wasm:   []extism.Wasm{extism.WasmFile{Path: "../build/qjs.wasm"}},
		Config: map[string]string{},
	}

	ctx := context.Background()
	var buf bytes.Buffer
	config := extism.PluginConfig{
		EnableWasi: true,
		ModuleConfig: wazero.NewModuleConfig().
			WithStdout(&buf),
	}
	plugin, err := extism.NewPlugin(ctx, manifest, config, []extism.HostFunction{})
	require.NoError(t, err)
	defer plugin.Close(ctx)

	_, _, err = plugin.Call("warmup", nil)
	require.NoError(t, err)

	_, _, err = plugin.Call("setEnv", []byte("{\"a\":\"b\",\"c\":\"d\"}"))
	require.NoError(t, err)

	_, _, err = plugin.Call("eval", []byte(`
import * as std from "qjs:std";
print(JSON.stringify(std.getenviron()));`))
	require.NoError(t, err)
	assert.Equal(t, "{\"a\":\"b\",\"c\":\"d\"}", strings.TrimSuffix(buf.String(), "\n"))
	buf.Reset()

	_, _, err = plugin.Call("eval", []byte("print(JSON.stringify(process.env));"))
	require.NoError(t, err)
	assert.Equal(t, "{\"a\":\"b\",\"c\":\"d\"}", strings.TrimSuffix(buf.String(), "\n"))
	buf.Reset()

	_, _, err = plugin.Call("unsetEnv", nil)
	require.NoError(t, err)

	_, _, err = plugin.Call("eval", []byte(`
import * as std from "qjs:std";
print(JSON.stringify(std.getenviron()));`))
	require.NoError(t, err)
	assert.Equal(t, "{}", strings.TrimSuffix(buf.String(), "\n"))
	buf.Reset()

	_, _, err = plugin.Call("eval", []byte("print(JSON.stringify(process.env));"))
	require.NoError(t, err)
	assert.Equal(t, "{}", strings.TrimSuffix(buf.String(), "\n"))
	buf.Reset()
}
