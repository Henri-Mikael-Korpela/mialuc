cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc
cmake --build build
.\build\mialuc.exe run .\test-code\sum.mu
