cmake_minimum_required(VERSION 3.10)

project(task-quickjs-ng LANGUAGES C)

add_subdirectory(quickjs)

add_compile_definitions(
    _WASI_EMULATED_PROCESS_CLOCKS
    _WASI_EMULATED_SIGNAL
)
add_link_options(
    -lwasi-emulated-process-clocks
    -lwasi-emulated-signal
)

xoption(QJS_ENABLE_CIVET "Civet integration" OFF)

add_executable(task_qjs
    qjs.c
)
target_sources(task_qjs PRIVATE quickjs/quickjs-libc.c)
set_target_properties(task_qjs PROPERTIES
    OUTPUT_NAME "qjs.wasm"
)
target_compile_definitions(task_qjs PRIVATE _GNU_SOURCE)
target_link_libraries(task_qjs qjs)
set_target_properties(task_qjs PROPERTIES ENABLE_EXPORTS TRUE)
