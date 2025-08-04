#define EXTISM_IMPLEMENTATION
#include "extism-pdk/extism-pdk.h"
#include "quickjs/quickjs.h"
#include "quickjs/quickjs-libc.h"
#include "quickjs/cutils.h"
#ifdef QJS_ENABLE_CIVET
#include "civet.h"
#endif

static JSValue js_navigator_get_userAgent(JSContext *ctx, JSValueConst this_val)
{
  char version[32];
  snprintf(version, sizeof(version), "quickjs-ng/%s", JS_GetVersion());
  return JS_NewString(ctx, version);
}

static JSValue js_gc(JSContext *ctx, JSValueConst this_val,
                     int argc, JSValueConst *argv)
{
  JS_RunGC(JS_GetRuntime(ctx));
  return JS_UNDEFINED;
}

static const JSCFunctionListEntry global_obj[] = {
    JS_CFUNC_DEF("gc", 0, js_gc),
};

static const JSCFunctionListEntry navigator_proto_funcs[] = {
    JS_CGETSET_DEF2("userAgent", js_navigator_get_userAgent, NULL, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Navigator", JS_PROP_CONFIGURABLE),
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

  JSValue navigator_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, navigator_proto, navigator_proto_funcs, countof(navigator_proto_funcs));
  JSValue navigator = JS_NewObjectProto(ctx, navigator_proto);
  JS_DefinePropertyValueStr(ctx, global, "navigator", navigator, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
  JS_FreeValue(ctx, global);
  JS_FreeValue(ctx, navigator_proto);

  js_std_add_helpers(ctx, -1, 0);

  return ctx;
}

static JSRuntime *rt;
static JSContext *ctx;
#ifdef QJS_ENABLE_CIVET
static JSValue civet_mod = JS_UNDEFINED;

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
  JS_FreeValue(ctx, mod_val);
  JS_FreeValue(ctx, obj);
  JS_FreeValue(ctx, val);
  js_free(ctx, write_obj_ptr);
  js_std_dump_error(ctx);
  return JS_UNDEFINED;
}
#endif

static bool init_js()
{
  rt = JS_NewRuntime();
  if (!rt)
  {
    extism_error_set(
        extism_alloc_buf_from_sz("cannot allocate JS runtime."));
    return false;
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
    return false;
  }

#ifdef QJS_ENABLE_CIVET
  civet_mod = load_civet();
#endif

  return true;
}

static void deinit_js()
{
  if (rt)
  {
    js_std_free_handlers(rt);
    if (ctx)
      JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
  }
}

static const char *get_config(const char *key)
{
  ExtismHandle value = extism_config_get(extism_alloc_buf_from_sz(key));

  if (value == 0)
    return 0;

  const uint64_t value_len = extism_length(value);

  uint8_t *value_data = malloc(value_len + 1);
  if (value_data == NULL)
    return 0;

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
        goto end;
      }
      val = JS_EvalFunction(ctx, val);
    }
    val = js_std_await(ctx, val);
  }
  else
    val = JS_Eval(ctx, buf, buf_len, filename, eval_flags);

end:
  return val;
}

#ifdef QJS_ENABLE_CIVET
static const char *civet_compile(const char *source)
{
  if (JS_IsUndefined(civet_mod))
    return NULL;

  JSValue fn = JS_GetPropertyStr(ctx, civet_mod, "Civet");
  fn = JS_GetPropertyStr(ctx, fn, "default");
  fn = JS_GetPropertyStr(ctx, fn, "compile");

  JSValue options = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, options, "sync", JS_NewBool(ctx, true));
  JSValue argv[2] = {
      JS_NewString(ctx, source),
      options,
  };
  JSValue ret = JS_Call(ctx, fn, JS_UNDEFINED, 2, argv);

  JS_FreeValue(ctx, fn);
  JS_FreeValue(ctx, argv[0]);
  JS_FreeValue(ctx, argv[1]);

  return JS_ToCString(ctx, ret);
}
#endif

/* -------------------------------- exported -------------------------------- */

int32_t EXTISM_EXPORTED_FUNCTION(warmup)
{
  return init_js() ? 0 : 1;
}

int32_t EXTISM_EXPORTED_FUNCTION(eval)
{
  const char *warmup_str = get_config("eval.warmup");
  bool warmup = true;
  if (warmup_str)
    warmup = strcmp(warmup_str, "true") == 0;
  free((void *)warmup_str);
  warmup_str = NULL;

  if (warmup)
  {
    if (!init_js())
      return 1;
  }
  else
  {
    if (!rt || !ctx)
    {
      extism_error_set(
          extism_alloc_buf_from_sz("JS runtime or context is not ready, did you warmup?"));
      return 1;
    }
  }

  const char *memory_limit_str = get_config("eval.memoryLimit");
  if (memory_limit_str)
  {
    int memory_limit = atoi(memory_limit_str);
    if (memory_limit >= 0)
      JS_SetMemoryLimit(rt, (size_t)&memory_limit);
  }
  free((void *)memory_limit_str);
  memory_limit_str = NULL;

  const char *stack_size_str = get_config("eval.stackSize");
  if (stack_size_str)
  {
    int stack_size = atoi(stack_size_str);
    if (stack_size >= 0)
      JS_SetMaxStackSize(rt, (size_t)&stack_size);
  }
  free((void *)stack_size_str);
  stack_size_str = NULL;

  uint64_t input_len = extism_input_length();
  uint8_t input_data[input_len + 1];
  extism_load_input(0, input_data, input_len);
  input_data[input_len] = '\0';
  const char *script = (char *)input_data;

  const char *dialect = get_config("eval.dialect");
#ifdef QJS_ENABLE_CIVET
  if (strcmp(dialect, "civet") == 0)
    script = civet_compile(script);
#endif
  free((void *)dialect);
  dialect = NULL;

  const char *module_str = get_config("eval.module");
  bool module;
  if (!module_str)
    module = JS_DetectModule(script, strlen(script));
  else
    module = strcmp(module_str, "true") == 0;
  free((void *)module_str);
  module_str = NULL;

  if (!module)
  {
    const char *str =
        "import * as bjson from 'qjs:bjson';\n"
        "import * as std from 'qjs:std';\n"
        "import * as os from 'qjs:os';\n"
        "globalThis.bjson = bjson;\n"
        "globalThis.std = std;\n"
        "globalThis.os = os;\n";
    eval_buf(ctx, str, strlen(str), "<input>", JS_EVAL_TYPE_MODULE);
  }
  int flags = module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
  JSValue val = eval_buf(ctx, script, strlen(script), "<eval>", flags);
  if (JS_IsException(val))
  {
    js_std_dump_error(ctx);
    JS_FreeValue(ctx, val);
    deinit_js();
    return 2;
  }

  JS_FreeValue(ctx, val);
  deinit_js();
  return 0;
}

#ifdef QJS_ENABLE_CIVET
int32_t EXTISM_EXPORTED_FUNCTION(civet)
{
  init_js();

  uint64_t input_len = extism_input_length();
  uint8_t input_data[input_len + 1];
  extism_load_input(0, input_data, input_len);
  input_data[input_len] = '\0';
  const char *script = (char *)input_data;
  script = civet_compile(script);

  const uint64_t len = strlen(script);
  ExtismHandle handle = extism_alloc(len);
  extism_store_to_handle(handle, 0, script, len);
  extism_output_set_from_handle(handle, 0, len);

  deinit_js();
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
#ifdef CIVET_VERSION
    ver = CIVET_VERSION;
#endif // CIVET_VERSION
  else
#endif // QJS_ENABLE_CIVET
    ver = (char *)JS_GetVersion();

  const uint64_t len = strlen(ver);
  ExtismHandle handle = extism_alloc(len);
  extism_store_to_handle(handle, 0, ver, len);
  extism_output_set_from_handle(handle, 0, len);
  return 0;
}
