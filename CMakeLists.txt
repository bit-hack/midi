cmake_minimum_required(VERSION 3.3)
project(midi)

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

add_library(libmidi libmidi.c libmidi.h)

add_executable(miditool miditool.c)
target_link_libraries(miditool libmidi)

add_executable(midiplay midiplay.c)
target_link_libraries(midiplay libmidi Winmm.lib)

configure_file(testmidi.py.in testmidi.py)
