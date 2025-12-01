cmake_minimum_required(VERSION 3.30)
project(rpflib)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory("vendors")

file(
        GLOB_RECURSE
        RPFLIB_FILES
        ${PROJECT_SOURCE_DIR}/include/*.h
        ${PROJECT_SOURCE_DIR}/src/*.h
        ${PROJECT_SOURCE_DIR}/src/*.cpp
)
add_library(${PROJECT_NAME} STATIC ${RPFLIB_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(${PROJECT_NAME} PUBLIC zlib)
