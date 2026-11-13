cmake_minimum_required(VERSION 3.16)
project(DialoguePratice VERSION 0.1.0 LANGUAGES C CXX)

# ---- 语言标准 -------------------------------------------------------------
# 服务端使用 C++17：g++ 11 完整支持，且涵盖 C++11/14 的全部特性。
# 若将来切到 g++ 13+，可把 CMAKE_CXX_STANDARD 改为 20/23 而不影响现有代码。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(common)
add_subdirectory(server)

# ---- 客户端（Qt6，后续阶段）------------------------------------------------
# 需要先安装: sudo apt install qt6-base-dev
# option(BUILD_CLIENT "Build the Qt6 client" OFF)
# if(BUILD_CLIENT)
#     add_subdirectory(client)
# endif()
