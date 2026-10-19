cmake_minimum_required(VERSION 3.16)
project(OpenDT VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(OPENDT_VENDOR_SDL "Download and build SDL2 statically instead of using an installed SDL2" OFF)
option(OPENDT_BUILD_TESTS "Build the unit tests" ON)

if(MSVC)
  # Sources contain UTF-8 string literals (the games' Japanese system messages).
  add_compile_options(/utf-8 /W3 /permissive- /Zc:__cplusplus)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall> $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
                      $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-parameter>)
endif()

find_package(Threads REQUIRED)

# ---- SDL2 (desktop player only) ----
set(OPENDT_SDL_TARGET "")
set(OPENDT_SDLMAIN_TARGET "")
if(OPENDT_VENDOR_SDL)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-2.30.9 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL2)
  set(OPENDT_SDL_TARGET SDL2::SDL2-static)
  if(TARGET SDL2::SDL2main)
    set(OPENDT_SDLMAIN_TARGET SDL2::SDL2main)
  endif()
else()
  find_package(SDL2 CONFIG QUIET)
  if(NOT SDL2_FOUND)
    find_package(SDL2 MODULE QUIET)
  endif()
  if(TARGET SDL2::SDL2)
    set(OPENDT_SDL_TARGET SDL2::SDL2)
  elseif(TARGET SDL2::SDL2-static)
    set(OPENDT_SDL_TARGET SDL2::SDL2-static)
  endif()
  if(TARGET SDL2::SDL2main)
    set(OPENDT_SDLMAIN_TARGET SDL2::SDL2main)
  endif()
endif()

# ---- ymfm (BSD-3-Clause): YM2203 emulation ----
add_library(ymfm STATIC third_party/ymfm/src/ymfm_opn.cpp third_party/ymfm/src/ymfm_ssg.cpp
                        third_party/ymfm/src/ymfm_adpcm.cpp third_party/ymfm/src/ymfm_misc.cpp)
target_include_directories(ymfm PUBLIC third_party/ymfm/src)
if(MSVC)
  target_compile_options(ymfm PRIVATE /w)
else()
  target_compile_options(ymfm PRIVATE -w)
endif()

# ---- engine libraries ----
file(GLOB CORE_SRC CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB MUSIC_SRC CONFIGURE_DEPENDS src/music/*.cpp)
file(GLOB D1_SRC CONFIGURE_DEPENDS src/d1/*.cpp)
file(GLOB D2_SRC CONFIGURE_DEPENDS src/d2/*.cpp)

add_library(core STATIC ${CORE_SRC})
target_include_directories(core PUBLIC src)
if(WIN32)
  target_link_libraries(core PUBLIC shell32)
endif()

add_library(music STATIC ${MUSIC_SRC})
target_link_libraries(music PUBLIC core ymfm Threads::Threads)

add_library(d1 STATIC ${D1_SRC})
target_link_libraries(d1 PUBLIC core)

add_library(d2 STATIC ${D2_SRC})
target_link_libraries(d2 PUBLIC core)

add_library(applib STATIC src/app/games.cpp)
target_link_libraries(applib PUBLIC core d1 d2 music)

# ---- executables ----
add_executable(opendt-headless src/app/headless.cpp)
target_link_libraries(opendt-headless applib)

add_executable(mmlplay src/app/mmlplay.cpp)
target_link_libraries(mmlplay music)

if(OPENDT_SDL_TARGET)
  add_executable(opendt src/app/sdl_main.cpp)
  target_link_libraries(opendt applib ${OPENDT_SDLMAIN_TARGET} ${OPENDT_SDL_TARGET} Threads::Threads)
  if(WIN32)
    set_target_properties(opendt PROPERTIES WIN32_EXECUTABLE TRUE)
  endif()
else()
  message(STATUS "SDL2 not found: the desktop player (opendt) will not be built")
endif()

if(OPENDT_BUILD_TESTS)
  file(GLOB TEST_SRC CONFIGURE_DEPENDS tests/*.cpp)
  add_executable(unit_tests ${TEST_SRC})
  target_link_libraries(unit_tests applib)
  enable_testing()
  add_test(NAME unit_tests COMMAND unit_tests)
endif()

install(TARGETS opendt-headless mmlplay RUNTIME DESTINATION bin)
if(TARGET opendt)
  install(TARGETS opendt RUNTIME DESTINATION bin)
endif()
