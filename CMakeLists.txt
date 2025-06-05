cmake_minimum_required(VERSION 3.20)
project(inmem_kv LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


include_directories(${PROJECT_SOURCE_DIR}/include)

file(GLOB_RECURSE KV_SOURCES src/*.cpp)

add_library(kv_lib STATIC ${KV_SOURCES})
target_include_directories(kv_lib PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(kv_lib PUBLIC cxx_std_23)

add_executable(kv_server main.cpp)
target_link_libraries(kv_server PRIVATE kv_lib ws2_32)

