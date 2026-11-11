cmake_minimum_required(VERSION 3.20)
project(MegaMan4Recomp C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(NESRECOMP_GAME_VERSION "0.1.0-development" CACHE STRING "Game release version")
set(NESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/nesrecomp" CACHE PATH "NESRecomp checkout")
set(NESRECOMP_BACKEND "cycle" CACHE STRING "CPU backend")
set(NESRECOMP_ROM "" CACHE FILEPATH "Original Mega Man 4 USA ROM")
if(NOT NESRECOMP_BACKEND STREQUAL "cycle")
    message(FATAL_ERROR "Mega Man 4 requires the cycle backend")
endif()
if(NOT EXISTS "${NESRECOMP_ROM}")
    message(FATAL_ERROR "Supply -DNESRECOMP_ROM with your Mega Man 4 (USA) ROM")
endif()
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
set(mm4_art "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded/packages/mega-man-4-usa.character.protoman/1.0.0/assets")
file(READ "${mm4_art}/protoman.png" MM4_PROTO_PNG_BYTES HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1,\n" MM4_PROTO_PNG_BYTES "${MM4_PROTO_PNG_BYTES}")
file(READ "${mm4_art}/jump-open.png" MM4_PROTO_JUMP_PNG_BYTES HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1,\n" MM4_PROTO_JUMP_PNG_BYTES "${MM4_PROTO_JUMP_PNG_BYTES}")
configure_file(src/mm4_proto_sheet.h.in "${CMAKE_CURRENT_BINARY_DIR}/mm4_proto_sheet.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${mm4_art}/protoman.png" "${mm4_art}/jump-open.png")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/rom_profile.py" "${NESRECOMP_ROM}"
    RESULT_VARIABLE profile_result OUTPUT_VARIABLE profile OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT profile_result EQUAL 0)
    message(FATAL_ERROR "Mega Man 4 ROM profile validation failed")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tools/rom_profile.py" "${NESRECOMP_ROM}")
include("${NESRECOMP_ROOT}/runner/cyc/project.cmake")
nesrecomp_add_cycle_game(MegaMan4Recomp
    ROM "${NESRECOMP_ROM}"
    GAME_CONFIG "${CMAKE_SOURCE_DIR}/game.toml"
    SEED_FILE "${CMAKE_SOURCE_DIR}/cyc_seeds_usa.txt"
    GAME_ID "mega-man-4-usa"
    PLAYERS 1
    REGION NTSC
    MODS GAME_ID "mega-man-4-usa"
    HOST_EXTRAS "${CMAKE_SOURCE_DIR}/src/mm4_host.c"
        "${CMAKE_SOURCE_DIR}/src/mm4_proto.c"
        "${CMAKE_SOURCE_DIR}/src/mm4_view.c"
        "${CMAKE_SOURCE_DIR}/src/mm4_world.c"
        "${CMAKE_SOURCE_DIR}/src/mm4_wide.c"
        "${NESRECOMP_ROOT}/runner/cyc/cyc_protoman_atlas.c")
target_include_directories(MegaMan4Recomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}" "${NESRECOMP_ROOT}/runner/src")
add_custom_command(TARGET MegaMan4Recomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/mods/preloaded" "$<TARGET_FILE_DIR:MegaMan4Recomp>/mods")
