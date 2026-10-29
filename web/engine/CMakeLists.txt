# The engine with its renderer (docs/MOTOR_WEB.md): the game module, the host that reads the user's game.pak
# (web/host/headless_host.c with SMG_HOST_AURORA) and Aurora on the browser's WebGPU (web/host/aurora_bridge.cpp). It
# runs inside a dedicated worker started by the page (web/shell), as smg_headless_web does; Aurora's render thread
# posts the XFB the game's VI shows to the page (web/shell/present.js).
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/SmgGame.cmake")

add_executable(smg_web
    "${CMAKE_CURRENT_SOURCE_DIR}/../host/headless_host.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../host/host_services.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../host/rvz_reader.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../host/console_services.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../host/aurora_bridge.cpp")
target_include_directories(smg_web PRIVATE "${SMG_NEBULA_NATIVE}/include" "${CMAKE_CURRENT_SOURCE_DIR}/../host"
                           "${SMG_AURORA_LIB}")
target_compile_definitions(smg_web PRIVATE SMG_HOST_AURORA=1)
target_compile_options(smg_web PRIVATE -pthread)
target_link_libraries(smg_web PRIVATE smg_game smg_zstd aurora_core_web aurora_vi_web)
# Aurora's API symbols that collide with the game's SDK are renamed in Aurora (web/aurora, SMG_AURORA_RENAMES).
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/../host/aurora_bridge.cpp" PROPERTIES
    COMPILE_OPTIONS "-include;${SMG_AURORA_RENAMES_HEADER}")
target_link_options(smg_web PRIVATE ${SMG_WEB_LINK_OPTIONS} -sASYNCIFY_STACK_SIZE=1048576 -sPTHREAD_POOL_SIZE=8
    -sENVIRONMENT=worker -sMODULARIZE -sEXPORT_NAME=createSmgModule -sINVOKE_RUN=0
    -lworkerfs.js "-sEXPORTED_RUNTIME_METHODS=['callMain','FS','WORKERFS']"
    "--extern-post-js=${CMAKE_CURRENT_SOURCE_DIR}/../shell/boot_worker.post.js"
    # smg_web.js.symbols (not published): wasm function indices to names, for the stack of an abort the page shows
    --emit-symbol-map)
set_property(TARGET smg_web APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../shell/boot_worker.post.js")
set_target_properties(smg_web PROPERTIES SUFFIX ".js")
smg_ppc_divide(smg_web)
