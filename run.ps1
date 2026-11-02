cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc
cmake --build build
.\build\mialuc.exe .\test-code\main-function.mu  