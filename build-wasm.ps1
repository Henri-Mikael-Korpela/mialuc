cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc
cmake --build build
.\build\mialuc.exe build --target wasm ./tests/wasm/sum.wasm .\test-code\sum.mu
