#!/bin/bash -e

mkdir -p build
rm -rf build/*

cmake -GNinja -B build -DCMAKE_BUILD_TYPE=Release .
ninja -C build

rm -rf build
