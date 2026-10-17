cmake_minimum_required(VERSION 3.10)
project(game_engine C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(NOT MSVC)
  add_compile_options(-Wall -Wextra)
endif()

# Platform-independent core: software renderer, animation, tilemap, physics, combat.
file(GLOB GE_SOURCES engine/src/*.c)
add_library(ge STATIC ${GE_SOURCES})
target_include_directories(ge PUBLIC engine/include)
find_library(M_LIB m)
if(M_LIB)
  target_link_libraries(ge PUBLIC ${M_LIB})
endif()

# Backends. A backend is just one source file implementing ge_platform.h, so it is
# compiled straight into each executable (a static library would create a
# circular dependency with the core).
find_package(SDL2 QUIET)
set(DEMO_SRC demo/main.c demo/art.c demo/backdrop.c demo/chars.c demo/world.c demo/puzzle.c demo/devlog.c demo/paragon.c demo/encounter.c demo/mods.c)

add_executable(demo_headless ${DEMO_SRC} platform/null.c)
target_compile_definitions(demo_headless PRIVATE DEMO_HEADLESS)
target_link_libraries(demo_headless ge)

if(SDL2_FOUND)
  add_executable(demo ${DEMO_SRC} platform/sdl2.c)
  target_link_libraries(demo ge)
  if(TARGET SDL2::SDL2)
    target_link_libraries(demo SDL2::SDL2)
  else()
    target_include_directories(demo PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(demo ${SDL2_LIBRARIES})
  endif()
else()
  message(STATUS "SDL2 not found: building only demo_headless (install the SDL2 dev package for the windowed demo)")
endif()

enable_testing()
add_executable(engine_tests tests/test_engine.c platform/null.c)
target_link_libraries(engine_tests ge)
target_compile_definitions(engine_tests PRIVATE GE_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
add_test(NAME engine_tests COMMAND engine_tests)
add_executable(fight_tests tests/test_fight.c)
target_link_libraries(fight_tests ge)
add_test(NAME fight_tests COMMAND fight_tests)

add_executable(world_tests tests/test_world.c demo/world.c)
target_link_libraries(world_tests ge)
add_test(NAME world_tests COMMAND world_tests)
add_executable(paragon_tests tests/test_paragon.c)
target_link_libraries(paragon_tests ge)
add_test(NAME paragon_tests COMMAND paragon_tests)

# the whole story end to end: walk into the Paragon's hall, lose to it as designed, see the report
add_test(NAME paragon_demo COMMAND demo_headless --autoplay --god --paragon --fresh --profile paragon_test_profile.txt --frames 9000)
set_tests_properties(paragon_demo PROPERTIES PASS_REGULAR_EXPRESSION "duels_lost=[12] ")
