cmake_minimum_required(VERSION 3.10)

project(quickjs-ng-extism LANGUAGES C)

add_subdirectory(quickjs)

add_compile_definitions(
    _WASI_EMULATED_PROCESS_CLOCKS
    _WASI_EMULATED_SIGNAL
)
add_link_options(
    -lwasi-emulated-process-clocks
    -lwasi-emulated-signal
)

add_executable(qjs_extism
    qjs.c
)
target_sources(qjs_extism PRIVATE quickjs/quickjs-libc.c)
set_target_properties(qjs_extism PROPERTIES
    OUTPUT_NAME "qjs.wasm"
)
target_compile_definitions(qjs_extism PRIVATE _GNU_SOURCE)
target_link_libraries(qjs_extism qjs)
set_target_properties(qjs_extism PROPERTIES ENABLE_EXPORTS TRUE)
