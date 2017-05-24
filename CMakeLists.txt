cmake_minimum_required(VERSION 3.7)
project(warp17)

set(CMAKE_CXX_STANDARD 11)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules/")
find_package(dpdk REQUIRED)

message("DPDK include dir: ${DPDK_INCLUDE_DIR}")

include_directories(${DPDK_INCLUDE_DIR})

FILE(GLOB_RECURSE SOURCE_FILES src/*.c)

MACRO(HEADER_DIRECTORIES return_list)
        FILE(GLOB_RECURSE new_list inc/*.h)
        SET(dir_list "")
        FOREACH(file_path ${new_list})
                GET_FILENAME_COMPONENT(dir_path ${file_path} PATH)
                SET(dir_list ${dir_list} ${dir_path})
        ENDFOREACH()
        LIST(REMOVE_DUPLICATES dir_list)
        SET(${return_list} ${dir_list})
ENDMACRO()

HEADER_DIRECTORIES(include_dirs)
message("folders: ${include_dirs}")
include_directories(${include_dirs})

add_executable(warp17 ${SOURCE_FILES})
target_link_libraries(warp17 ${DPDK_LIBRARIES})