cmake_minimum_required(VERSION 3.20)
project(pow_mining_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler optimization settings
if(MSVC)
    add_compile_options(/O2 /W4 /permissive- /utf-8)
else()
    add_compile_options(-O3 -Wall -Wextra -Wpedantic)
endif()

include_directories(include)

# Core engine library
set(CORE_SOURCES
    src/crypto/sha256.cpp
    src/chain/block.cpp
    src/chain/merkle.cpp
    src/miner/miner_pool.cpp
    src/server/http_server.cpp
)

add_library(pow_core STATIC ${CORE_SOURCES})
target_include_directories(pow_core PUBLIC include)

if(WIN32)
    target_link_libraries(pow_core PUBLIC ws2_32)
else()
    target_link_libraries(pow_core PUBLIC pthread atomic)
endif()

# Main mining engine executable
add_executable(pow_engine src/main.cpp)
target_link_libraries(pow_engine PRIVATE pow_core)

# Unit test suite executable
add_executable(test_pow tests/test_pow.cpp)
target_link_libraries(test_pow PRIVATE pow_core)

# Copy web dashboard directory to build output
add_custom_command(TARGET pow_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/web
        $<TARGET_FILE_DIR:pow_engine>/web
)
