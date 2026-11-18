cmake_minimum_required(VERSION 3.20)
project(mialuc C)

# The project is written in C99, see README.md.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(mialuc src/main.c src/array_slice.c src/ast.c src/constant.c src/package.c src/parser.c src/semantic.c src/str.c src/tokenizer.c src/wasm.c src/wasm-text.c src/x64.c src/x64-text.c src/process.c)

# Headers live directly in src/ but are included as <mialu/...>, so they are copied into a
# generated include directory under that name. configure_file reruns CMake when a header changes.
set(MIALU_INCLUDE_DIRECTORY ${CMAKE_BINARY_DIR}/include)
file(GLOB MIALU_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.h)
foreach(header_path ${MIALU_HEADERS})
    get_filename_component(header_name ${header_path} NAME)
    configure_file(${header_path} ${MIALU_INCLUDE_DIRECTORY}/mialu/${header_name} COPYONLY)
endforeach()
target_include_directories(mialuc PRIVATE ${MIALU_INCLUDE_DIRECTORY})

# The standard library of Mialu is the directory src/mialu-std, which a build from the sources of
# this project, debug or release, finds its files in. A release puts the same files in the directory
# lib next to the compiler, which is looked in first, see package.h.
target_compile_definitions(mialuc PRIVATE MIALU_STD_DIRECTORY="${CMAKE_SOURCE_DIR}/src/mialu-std")

# Warnings are errors in our own code, per the style guide. -Wpedantic rejects anything beyond C99.
target_compile_options(mialuc PRIVATE -Wall -Wextra -Wpedantic -Werror)
