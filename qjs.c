#define EXTISM_IMPLEMENTATION
#include "extism-pdk/extism-pdk.h"
#include "quickjs/quickjs.h"
#include <stdint.h>
#include <string.h>

int32_t EXTISM_EXPORTED_FUNCTION(getVersion)
{
  const char *ver = JS_GetVersion();
  const uint64_t len = strlen(ver);
  ExtismHandle handle = extism_alloc(len);
  extism_store_to_handle(handle, 0, ver, len);
  extism_output_set_from_handle(handle, 0, len);
  return 0;
}
