cmake_minimum_required(VERSION 3.28)
# Only set the cxx_standard if it is not set by someone else
if (NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()

# strongly encouraged to enable this globally to avoid conflicts between
# -Wpedantic being enabled and -std=c++20 and -std=gnu++20 for example
# when compiling with PCH enabled
set(CMAKE_CXX_EXTENSIONS OFF)

# Set the project name and language
project(
        coroutine_execution_practice
        VERSION 0.0.1
        DESCRIPTION "simple task scheduling system"
        LANGUAGES CXX)

include(cmake/Dependencies.cmake)
setup_dependencies()

include(cmake/SystemLink.cmake)
add_subdirectory(examples)
