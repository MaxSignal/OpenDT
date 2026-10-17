cmake_minimum_required(VERSION 3.16)
project(Dante98CR CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

find_package(ZLIB REQUIRED)
find_package(SDL2)
find_package(Threads REQUIRED)

# ymfm (BSD-3-Clause) - YM2203 emulation
add_library(ymfm STATIC third_party/ymfm/src/ymfm_opn.cpp third_party/ymfm/src/ymfm_ssg.cpp
                        third_party/ymfm/src/ymfm_adpcm.cpp third_party/ymfm/src/ymfm_misc.cpp)
target_include_directories(ymfm PUBLIC third_party/ymfm/src)
target_compile_options(ymfm PRIVATE -w)

# Each module globs its own directory so different modules can be developed independently.
file(GLOB CORE_SRC CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB MUSIC_SRC CONFIGURE_DEPENDS src/music/*.cpp)
file(GLOB D1_SRC CONFIGURE_DEPENDS src/d1/*.cpp)
file(GLOB D2_SRC CONFIGURE_DEPENDS src/d2/*.cpp)

add_library(core STATIC ${CORE_SRC})
target_include_directories(core PUBLIC src)
target_link_libraries(core PUBLIC ZLIB::ZLIB)

add_library(music STATIC ${MUSIC_SRC})
target_link_libraries(music PUBLIC core ymfm Threads::Threads)

add_library(d1 STATIC ${D1_SRC})
target_link_libraries(d1 PUBLIC core)

add_library(d2 STATIC ${D2_SRC})
target_link_libraries(d2 PUBLIC core)

add_library(applib STATIC src/app/games.cpp)
target_link_libraries(applib PUBLIC core d1 d2 music)

add_executable(dante98cr-headless src/app/headless.cpp)
target_link_libraries(dante98cr-headless applib)

if(SDL2_FOUND)
  add_executable(dante98cr src/app/sdl_main.cpp)
  target_include_directories(dante98cr PRIVATE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(dante98cr applib ${SDL2_LIBRARIES} Threads::Threads)
endif()

# Unit tests
file(GLOB TEST_SRC CONFIGURE_DEPENDS tests/*.cpp)
if(TEST_SRC)
  add_executable(unit_tests ${TEST_SRC})
  target_link_libraries(unit_tests applib)
endif()
