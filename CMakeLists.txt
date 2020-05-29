cmake_minimum_required(VERSION 3.16)
project(eventModule)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CAMKE_CXX_FLAGS} -std=c++11 -pthread -lm")
set(CMAKE_EXE_LINKER_FLAGS "-pthread -lm")
set(CMAKE_C_FLAGS "-std=c++11 -Wall -Wpointer-arith -fPIC")

include_directories(src)
include_directories(src/core)
include_directories(src/common)

aux_source_directory(src SRC)
aux_source_directory(src/core CORE)
aux_source_directory(src/common COMMON)
add_executable(eventModule src/main.cpp ${SRC} ${CORE} ${COMMON})

add_executable(testCli testCli.cpp ${COMMON})