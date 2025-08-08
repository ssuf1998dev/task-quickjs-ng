#define EXTISM_IMPLEMENTATION
#include "extism-pdk/extism-pdk.h"
#include "quickjs/quickjs.h"
#include "quickjs/quickjs-libc.h"
#include "quickjs/cutils.h"
#ifdef QJS_ENABLE_CIVET
#include "civet.h"
#endif

static JSValue js_gc(JSContext *ctx, JSValueConst this_val,
                     int argc, JSValueConst *argv)
{
  JS_RunGC(JS_GetRuntime(ctx));
  return JS_UNDEFINED;
}

static const JSCFunctionListEntry global_obj[] = {
    JS_CFUNC_DEF("gc", 0, js_gc),
};

static JSContext *JS_NewCustomContext(JSRuntime *rt)
{
  JSContext *ctx = JS_NewContext(rt);
  if (!ctx)
    return NULL;

  js_init_module_std(ctx, "qjs:std");
  js_init_module_os(ctx, "qjs:os");
  js_init_module_bjson(ctx, "qjs:bjson");

  JSValue global = JS_GetGlobalObject(ctx);
  JS_SetPropertyFunctionList(ctx, global, global_obj, countof(global_obj));

  JSValue js_process = JS_NewObject(ctx);
  JSValue js_process_env = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, js_process, "env", js_process_env);
  JS_SetPropertyStr(ctx, global, "process", js_process);

  JS_FreeValue(ctx, global);

  js_std_add_helpers(ctx, -1, 0);

  return ctx;
}

static JSRuntime *rt;
static JSContext *ctx;
#ifdef QJS_ENABLE_CIVET
static JSValue civet_mod = JS_UNINITIALIZED;

static JSValue load_civet()
{
  JSModuleDef *mod_def;
  JSValue obj, val;

  Civet_dist_quickjs_min_mjs[Civet_dist_quickjs_min_mjs_len] = '\0';
  JSValue mod_val = JS_Eval(ctx, (char *)Civet_dist_quickjs_min_mjs, Civet_dist_quickjs_min_mjs_len, "civet.min.mjs", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

  uint8_t *write_obj_ptr;

  if (JS_IsException(mod_val))
    goto exception;

  size_t write_obj_len = 0;
  write_obj_ptr = JS_WriteObject(ctx, &write_obj_len, mod_val, JS_WRITE_OBJ_BYTECODE);
  if (!write_obj_ptr || write_obj_len <= 0)
    goto exception;

  JS_FreeValue(ctx, mod_val);
  obj = JS_ReadObject(ctx, write_obj_ptr, write_obj_len, JS_READ_OBJ_BYTECODE);
  js_free(ctx, write_obj_ptr);
  if (JS_IsException(obj) || !JS_IsModule(obj))
    goto exception;
  if (JS_ResolveModule(ctx, obj) < 0 || js_module_set_import_meta(ctx, obj, false, true) < 0)
    goto exception;

  val = JS_EvalFunction(ctx, JS_DupValue(ctx, obj));
  val = js_std_await(ctx, val);
  if (JS_IsException(val))
    goto exception;

  JS_FreeValue(ctx, val);
  mod_def = JS_VALUE_GET_PTR(obj);
  JS_FreeValue(ctx, obj);
  return JS_GetModuleNamespace(ctx, mod_def);

exception:
  if (!JS_IsUninitialized(mod_val))
    JS_FreeValue(ctx, mod_val);

  if (!JS_IsUninitialized(obj))
    JS_FreeValue(ctx, obj);

  if (!JS_IsUninitialized(val))
    JS_FreeValue(ctx, val);

  if (write_obj_ptr)
    js_free(ctx, write_obj_ptr);

  return JS_UNINITIALIZED;
}
#endif

static int init_js()
{
  rt = JS_NewRuntime();
  if (!rt)
  {
    extism_error_set(
        extism_alloc_buf_from_sz("cannot allocate JS runtime."));
    return 1;
  }

  js_std_set_worker_new_context_func(JS_NewCustomContext);
  js_std_init_handlers(rt);

  JS_SetModuleLoaderFunc(rt, NULL, js_module_loader, NULL);
  JS_SetHostPromiseRejectionTracker(rt, js_std_promise_rejection_tracker, NULL);

  ctx = JS_NewCustomContext(rt);
  if (!ctx)
  {
    extism_error_set(
        extism_alloc_buf_from_sz("cannot allocate JS context."));
    return 1;
  }

#ifdef QJS_ENABLE_CIVET
  civet_mod = load_civet();
#endif

  return 0;
}

static void deinit_js()
{
  if (ctx)
  {
#ifdef QJS_ENABLE_CIVET
    if (!JS_IsUninitialized(civet_mod))
    {
      JS_FreeValue(ctx, civet_mod);
      civet_mod = JS_UNINITIALIZED;
    }
#endif
    JS_FreeContext(ctx);
  }
  ctx = NULL;

  if (rt)
  {
    js_std_free_handlers(rt);
    JS_FreeRuntime(rt);
  }
  rt = NULL;
}

static const char *get_config(const char *key)
{
  ExtismHandle value = extism_config_get(extism_alloc_buf_from_sz(key));

  if (value == 0)
    return NULL;

  const uint64_t value_len = extism_length(value);

  uint8_t *value_data = malloc(value_len + 1);
  if (value_data == NULL)
    return NULL;

  extism_load_from_handle(value, 0, value_data, value_len);
  value_data[value_len] = '\0';
  return (char *)value_data;
}

static JSValue eval_buf(JSContext *ctx, const void *buf, int buf_len,
                        const char *filename, int eval_flags)
{
  bool use_realpath;
  JSValue val;

  if ((eval_flags & JS_EVAL_TYPE_MASK) == JS_EVAL_TYPE_MODULE)
  {
    /* for the modules, we compile then run to be able to set
       import.meta */
    val = JS_Eval(ctx, buf, buf_len, filename,
                  eval_flags | JS_EVAL_FLAG_COMPILE_ONLY);
    if (!JS_IsException(val))
    {
      // ex. "<cmdline>" pr "/dev/stdin"
      use_realpath =
          !(*filename == '<' || !strncmp(filename, "/dev/", 5));
      if (js_module_set_import_meta(ctx, val, use_realpath, true) < 0)
      {
        val = JS_GetException(ctx);
        return val;
      }
      val = JS_EvalFunction(ctx, val);
    }
    val = js_std_await(ctx, val);
  }
  else
    val = JS_Eval(ctx, buf, buf_len, filename, eval_flags);

  return val;
}

#ifdef QJS_ENABLE_CIVET
static JSValue civet_compile(const char *source)
{
  if (JS_IsUninitialized(civet_mod))
    return JS_UNINITIALIZED;

  JSValue export = JS_GetPropertyStr(ctx, civet_mod, "Civet");
  JSValue d = JS_GetPropertyStr(ctx, export, "default");
  JSValue fn = JS_GetPropertyStr(ctx, d, "compile");

  JSValue options = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, options, "sync", JS_NewBool(ctx, true));
  JSValue argv[2] = {
      JS_NewString(ctx, source),
      options,
  };
  JSValue ret = JS_Call(ctx, fn, JS_UNDEFINED, 2, argv);

  JS_FreeValue(ctx, fn);
  JS_FreeValue(ctx, d);
  JS_FreeValue(ctx, export);
  JS_FreeValue(ctx, argv[0]);
  JS_FreeValue(ctx, argv[1]);

  JSValue err = JS_GetException(ctx);
  if (!JS_IsUninitialized(err))
    return err;

  return ret;
}
#endif

static int guard()
{
  if (!rt || !ctx)
  {
    extism_error_set(
        extism_alloc_buf_from_sz("JS runtime or context is not ready, did you warmup?"));
    return 1;
  }

  return 0;
}

/* -------------------------------- exported -------------------------------- */

int32_t EXTISM_EXPORTED_FUNCTION(warmup)
{
  return init_js();
}

int32_t EXTISM_EXPORTED_FUNCTION(cleanup)
{
  deinit_js();
  return 0;
}

int32_t EXTISM_EXPORTED_FUNCTION(setEnv)
{
  if (guard())
    return 1;

  uint64_t input_len = extism_input_length();
  uint8_t input_data[input_len + 1];
  extism_load_input(0, input_data, input_len);
  input_data[input_len] = '\0';
  char *json_str = (char *)input_data;

  JSValue json = JS_ParseJSON(ctx, json_str, strlen(json_str), "<json>");
  if (!JS_IsObject(json))
  {
    extism_error_set(
        extism_alloc_buf_from_sz("Input should be an object."));
    return 2;
  }

  JSValue global = JS_GetGlobalObject(ctx);
  JSValue js_process = JS_GetPropertyStr(ctx, global, "process");
  JS_SetPropertyStr(ctx, js_process, "env", JS_DupValue(ctx, json));

  const char *setenv_script =
      "import { setenv } from 'qjs:std';\n"
      "Object.entries(process.env).forEach((entry)=>setenv(...entry))\n";
  eval_buf(ctx, setenv_script, strlen(setenv_script), "<setenv>", JS_EVAL_TYPE_MODULE);
  free((void *)setenv_script);
  setenv_script = NULL;

  JS_FreeValue(ctx, json);
  JS_FreeValue(ctx, global);

  return 0;
}

int32_t EXTISM_EXPORTED_FUNCTION(unsetEnv)
{
  if (guard())
    return 1;

  const char *unsetenv_script =
      "import { unsetenv, getenviron } from 'qjs:std';\n"
      "Object.keys(getenviron()??{}).forEach(key=>unsetenv(key))\n"
      "process.env={};\n";
  eval_buf(ctx, unsetenv_script, strlen(unsetenv_script), "<unsetenv>", JS_EVAL_TYPE_MODULE);
  return 0;
}

int32_t EXTISM_EXPORTED_FUNCTION(eval)
{
  if (guard())
    return 1;

  const char *
      memory_limit_str = get_config("eval.memoryLimit");
  if (memory_limit_str)
  {
    int memory_limit = atoi(memory_limit_str);
    if (memory_limit >= 0)
      JS_SetMemoryLimit(rt, (size_t)memory_limit);
    free((void *)memory_limit_str);
    memory_limit_str = NULL;
  }

  const char *stack_size_str = get_config("eval.stackSize");
  if (stack_size_str)
  {
    int stack_size = atoi(stack_size_str);
    if (stack_size >= 0)
      JS_SetMaxStackSize(rt, (size_t)stack_size);
    free((void *)stack_size_str);
    stack_size_str = NULL;
  }

  uint64_t input_len = extism_input_length();
  uint8_t input_data[input_len + 1];
  extism_load_input(0, input_data, input_len);
  input_data[input_len] = '\0';
  const char *script = (char *)input_data;

  const char *dialect = get_config("eval.dialect");
  if (dialect)
  {
#ifdef QJS_ENABLE_CIVET
    if (strcmp(dialect, "civet") == 0)
    {
      JSValue compiled = civet_compile(script);
      const char *compiled_str = JS_ToCString(ctx, compiled);
      JS_FreeValue(ctx, compiled);

      if (!JS_IsString(compiled))
      {
        extism_error_set(
            extism_alloc_buf_from_sz(compiled_str));
        return 2;
      }
      script = compiled_str;
    }
#endif
    free((void *)dialect);
    dialect = NULL;
  }

  const char *module_str = get_config("eval.module");
  bool module;
  if (!module_str)
    module = JS_DetectModule(script, strlen(script));
  else
    module = strcmp(module_str, "true") == 0;
  free((void *)module_str);
  module_str = NULL;

  const char *dir = get_config("eval.dir");
  if (dir)
  {
    char *chdir_script;
    sprintf(chdir_script, "import { chdir, getcwd } from 'qjs:os';chdir(`%s`);", dir);
    eval_buf(ctx, chdir_script, strlen(chdir_script), "<chdir>", JS_EVAL_TYPE_MODULE);
    free((void *)dir);
    dir = NULL;
    free((void *)chdir_script);
    chdir_script = NULL;
  }

  if (!module)
  {
    const char *shims_script =
        "import * as bjson from 'qjs:bjson';\n"
        "import * as std from 'qjs:std';\n"
        "import * as os from 'qjs:os';\n"
        "globalThis.bjson = bjson;\n"
        "globalThis.std = std;\n"
        "globalThis.os = os;\n";
    eval_buf(ctx, shims_script, strlen(shims_script), "<shims>", JS_EVAL_TYPE_MODULE);
    free((void *)shims_script);
    shims_script = NULL;
  }
  int flags = module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
  JSValue val = eval_buf(ctx, script, strlen(script), "<eval>", flags);
  free((void *)script);
  if (JS_IsException(val))
  {
    extism_error_set(
        extism_alloc_buf_from_sz(JS_ToCString(ctx, val)));
    JS_FreeValue(ctx, val);
    return 2;
  }

  JS_FreeValue(ctx, val);
  return 0;
}

#ifdef QJS_ENABLE_CIVET
int32_t EXTISM_EXPORTED_FUNCTION(civet)
{
  if (guard())
    return 1;

  uint64_t input_len = extism_input_length();
  uint8_t input_data[input_len + 1];
  extism_load_input(0, input_data, input_len);
  input_data[input_len] = '\0';
  const char *script = (char *)input_data;

  JSValue compiled = civet_compile(script);
  const char *compiled_str = JS_ToCString(ctx, compiled);
  JS_FreeValue(ctx, compiled);

  if (!JS_IsString(compiled))
  {
    extism_error_set(
        extism_alloc_buf_from_sz(compiled_str));
    return 2;
  }
  script = compiled_str;

  const uint64_t len = strlen(script);
  ExtismHandle handle = extism_alloc(len);
  extism_store_to_handle(handle, 0, script, len);
  extism_output_set_from_handle(handle, 0, len);

  free((void *)script);

  return 0;
}
#endif

int32_t EXTISM_EXPORTED_FUNCTION(getVersion)
{
  uint64_t input_len = extism_input_length();
  uint8_t input_data[input_len + 1];
  extism_load_input(0, input_data, input_len);
  input_data[input_len] = '\0';
  const char *key = (char *)input_data;

  char *ver;
#ifdef QJS_ENABLE_CIVET
  if (strcmp(key, "civet") == 0)
  {
#ifdef CIVET_VERSION
    ver = CIVET_VERSION;
#endif // CIVET_VERSION
  }
  else
#endif // QJS_ENABLE_CIVET
  {
    ver = (char *)JS_GetVersion();
  }

  const uint64_t len = strlen(ver);
  ExtismHandle handle = extism_alloc(len);
  extism_store_to_handle(handle, 0, ver, len);
  extism_output_set_from_handle(handle, 0, len);
  return 0;
}
