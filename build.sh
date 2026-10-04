#!/bin/bash -e

mkdir -p build

cmake -B build -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build -j$(nproc)
