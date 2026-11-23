cmake_minimum_required(VERSION 3.20)
# The version of the compiler is written once, in the file VERSION, as MAJOR.MINOR.PATCH on one line.
# The compilers get it as the macro MIALU_VERSION_STRING, see below, and scripts/bump-version.mu and
# scripts/deploy-release.mu read the same file. A change of the file configures the project again.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION MIALU_VERSION LIMIT_COUNT 1)
if(NOT MIALU_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "VERSION must be a version of the form MAJOR.MINOR.PATCH, such as 0.2.0, "
                        "and it is '${MIALU_VERSION}'.")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION)

project(mialuc VERSION ${MIALU_VERSION} LANGUAGES C)

# The commands of the compiler, with the definitions, go to compile_commands.json in the directory of
# the build, which clangd and the other tools that use Clang or GCC read to know how a file is built.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The project is written in C99, see README.md.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(mialuc src/main.c src/array_slice.c src/ast.c src/constant.c src/diagnostic.c src/package.c src/parser.c src/report.c src/semantic.c src/str.c src/tokenizer.c src/wasm.c src/wasm-text.c src/x64.c src/x64-text.c src/process.c)

# Headers live directly in src/ but are included as <mialu/...>, so they are copied into a
# generated include directory under that name. configure_file reruns CMake when a header changes.
set(MIALU_INCLUDE_DIRECTORY ${CMAKE_BINARY_DIR}/include)
file(GLOB MIALU_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.h)
foreach(header_path ${MIALU_HEADERS})
    get_filename_component(header_name ${header_path} NAME)
    configure_file(${header_path} ${MIALU_INCLUDE_DIRECTORY}/mialu/${header_name} COPYONLY)
endforeach()
target_include_directories(mialuc PRIVATE ${MIALU_INCLUDE_DIRECTORY})

# The version is a definition of the command line of the compiler, as GCC and Clang take it with
# -D, which is also in compile_commands.json for the tools that read it. A string, such as 0.4.0,
# so it is pasted next to other literals: "mialuc " MIALU_VERSION_STRING.
target_compile_definitions(mialuc PRIVATE MIALU_VERSION_STRING="${MIALU_VERSION}")

# The standard library of Mialu is the directory src/mialu-std, which a build from the sources of
# this project, debug or release, finds its files in. A release puts the same files in the directory
# lib next to the compiler, which is looked in first, see package.h.
target_compile_definitions(mialuc PRIVATE MIALU_STD_DIRECTORY="${CMAKE_SOURCE_DIR}/src/mialu-std")

# Warnings are errors in our own code, per the style guide. -Wpedantic rejects anything beyond C99.
target_compile_options(mialuc PRIVATE -Wall -Wextra -Wpedantic -Werror)

# mialudoc is a program in Mialu, an HTTP server that shows the documents of docs/ as pages of HTML, so it is
# built by the compiler that was just built and not by the C compiler. It needs the target x64-win32,
# so it is only a target on Windows. Run it as `mialudoc [port [directory]]`, see the comments of
# src/mialudoc/mialudoc.mu, which also say what Mialu is missing for a program of this kind.
if(WIN32)
    file(GLOB MIALUDOC_STANDARD_LIBRARY CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/mialu-std/*.mu)
    set(MIALUDOC_SOURCE ${CMAKE_SOURCE_DIR}/src/mialudoc/mialudoc.mu)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/mialudoc.exe
        COMMAND mialuc build --target x64-win32 -o ${CMAKE_BINARY_DIR}/mialudoc.exe ${MIALUDOC_SOURCE}
        DEPENDS mialuc ${MIALUDOC_SOURCE} ${MIALUDOC_STANDARD_LIBRARY}
        COMMENT "Building mialudoc with mialuc"
        VERBATIM)
    add_custom_target(mialudoc ALL DEPENDS ${CMAKE_BINARY_DIR}/mialudoc.exe)
endif()
