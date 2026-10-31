cmake_minimum_required(VERSION 3.20)
project(mialuc C)

# The project is written in C99, see README.md.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(mialuc src/main.c)

# Warnings are errors in our own code, per the style guide. -Wpedantic rejects anything beyond C99.
target_compile_options(mialuc PRIVATE -Wall -Wextra -Wpedantic -Werror)
