cmake_minimum_required(VERSION 3.20)
project(mialuc C)

# The project is written in C99, see README.md.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(mialuc src/main.c src/array_slice.c src/ast.c src/parser.c src/str.c src/tokenizer.c src/wasm.c src/wasm-text.c)

# Headers live directly in src/ but are included as <mialu/...>, so they are copied into a
# generated include directory under that name. configure_file reruns CMake when a header changes.
set(MIALU_INCLUDE_DIRECTORY ${CMAKE_BINARY_DIR}/include)
file(GLOB MIALU_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.h)
foreach(header_path ${MIALU_HEADERS})
    get_filename_component(header_name ${header_path} NAME)
    configure_file(${header_path} ${MIALU_INCLUDE_DIRECTORY}/mialu/${header_name} COPYONLY)
endforeach()
target_include_directories(mialuc PRIVATE ${MIALU_INCLUDE_DIRECTORY})

# Warnings are errors in our own code, per the style guide. -Wpedantic rejects anything beyond C99.
target_compile_options(mialuc PRIVATE -Wall -Wextra -Wpedantic -Werror)
