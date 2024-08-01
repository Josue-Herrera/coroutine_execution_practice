set(HEADER_DIR ${CMAKE_SOURCE_DIR}/examples)
FILE(GLOB SOURCE_FILES ${CMAKE_SOURCE_DIR}/examples/*.cc ${CMAKE_SOURCE_DIR}/examples/*.cpp)
FILE(GLOB HEADER_FILES ${CMAKE_SOURCE_DIR}/include/*.hpp ${CMAKE_SOURCE_DIR}/include/*.h)

add_executable(examples main.cpp ${SOURCE_FILES} ${HEADER_FILES})

target_include_directories(examples PRIVATE ${HEADER_DIR})

set(CLANG_COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Werror")
set(CLANG_COMPILE_DEBUG_OPTIONS "${CLANG_COMPILE_OPTIONS};-O0;")
set(CLANG_COMPILE_RELEASE_OPTIONS "${CLANG_COMPILE_OPTIONS};-O3;")

target_compile_options(examples PUBLIC "$<$<CONFIG:Debug>:${CLANG_COMPILE_DEBUG_OPTIONS}>")
target_compile_options(examples PUBLIC "$<$<CONFIG:Release>:${CLANG_COMPILE_DEBUG_OPTIONS}>")

target_link_system_libraries(
    examples
  PRIVATE
    fmt::fmt
    spdlog::spdlog
    range-v3::range-v3
    STDEXEC::stdexec
)
