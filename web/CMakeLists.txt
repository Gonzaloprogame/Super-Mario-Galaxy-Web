# The whole web build: the game module and its headless hosts (game/), Aurora on the browser's WebGPU (aurora/), the
# bridge from the game's GX stream to Aurora and its test (bridge/), and the engine with its renderer (engine/).
#   emcmake cmake -S web -B build/all -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.24)
project(SmgWeb LANGUAGES C CXX)

add_subdirectory(game)
add_subdirectory(aurora)
add_subdirectory(bridge)
add_subdirectory(engine)
