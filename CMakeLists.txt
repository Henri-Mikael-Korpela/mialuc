cmake_minimum_required(VERSION 3.20)
project(mialuc C)

# The project is written in C99, see README.md.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(mialuc src/main.c)

# Headers live directly in src/ but are included as <mialu/...>, so they are copied into a
# generated include directory under that name. configure_file reruns CMake when a header changes.
set(MIALU_INCLUDE_DIRECTORY ${CMAKE_BINARY_DIR}/include)
foreach(header static_assert.h str.h)
    configure_file(src/${header} ${MIALU_INCLUDE_DIRECTORY}/mialu/${header} COPYONLY)
endforeach()
target_include_directories(mialuc PRIVATE ${MIALU_INCLUDE_DIRECTORY})

# Warnings are errors in our own code, per the style guide. -Wpedantic rejects anything beyond C99.
target_compile_options(mialuc PRIVATE -Wall -Wextra -Wpedantic -Werror)
