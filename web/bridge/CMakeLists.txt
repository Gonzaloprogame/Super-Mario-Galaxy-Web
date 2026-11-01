# The bridge test (docs/PUENTE_FIFO.md): the game module's GX library draws a test scene, its FIFO stream goes through
# the host to Aurora on the browser's WebGPU, and the EFB is read back.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/SmgGame.cmake")

# The scene uses the game's SDK headers: built with the game module's options.
add_library(smg_bridge_scene OBJECT gx_bridge_scene.c)
smg_game_options(smg_bridge_scene)

add_executable(gx_bridge
    bridge_main.c
    "${CMAKE_CURRENT_SOURCE_DIR}/../host/host_services.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../host/aurora_bridge.cpp"
    $<TARGET_OBJECTS:smg_bridge_scene>)
target_include_directories(gx_bridge PRIVATE "${SMG_NEBULA_NATIVE}/include" "${CMAKE_CURRENT_SOURCE_DIR}/../host"
                           "${SMG_AURORA_LIB}")
target_compile_options(gx_bridge PRIVATE -pthread)
# Aurora's API symbols that collide with the game's SDK are renamed in Aurora (web/aurora, SMG_AURORA_RENAMES): the GX
# functions the scene calls are the game's.
target_link_libraries(gx_bridge PRIVATE smg_game aurora_core_web aurora_vi_web)
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/../host/aurora_bridge.cpp" PROPERTIES
    COMPILE_OPTIONS "-include;${SMG_AURORA_RENAMES_HEADER}")
# PROXY_TO_PTHREAD: main (the game's simulation thread) runs off the page's thread; Aurora has its own render thread.
target_link_options(gx_bridge PRIVATE ${SMG_WEB_LINK_OPTIONS} -sASYNCIFY_STACK_SIZE=1048576 -sPROXY_TO_PTHREAD
    -sPTHREAD_POOL_SIZE=6 -sENVIRONMENT=web,worker)
set_target_properties(gx_bridge PROPERTIES SUFFIX ".js")
smg_ppc_divide(gx_bridge)

# Replaying a GX trace (a renderer failure's, from the engine's log) through the same bridge, without the game
# (gx_replay.html, tools/gx_trace.py for the walk by hand).
add_executable(gx_replay gx_replay_main.c "${CMAKE_CURRENT_SOURCE_DIR}/../host/aurora_bridge.cpp")
target_include_directories(gx_replay PRIVATE "${SMG_NEBULA_NATIVE}/include" "${CMAKE_CURRENT_SOURCE_DIR}/../host"
                           "${SMG_AURORA_LIB}")
target_compile_options(gx_replay PRIVATE -pthread)
target_link_libraries(gx_replay PRIVATE aurora_core_web aurora_vi_web)
# Without the game, nothing defines the game's operator new: the link options without their --wrap of it
set(replay_link_options ${SMG_WEB_LINK_OPTIONS})
list(FILTER replay_link_options EXCLUDE REGEX "--wrap")
target_link_options(gx_replay PRIVATE ${replay_link_options} -sASYNCIFY_STACK_SIZE=1048576 -sPROXY_TO_PTHREAD
    -sPTHREAD_POOL_SIZE=6 -sENVIRONMENT=web,worker "-sEXPORTED_RUNTIME_METHODS=['FS']" -sFORCE_FILESYSTEM)
set_target_properties(gx_replay PROPERTIES SUFFIX ".js")
