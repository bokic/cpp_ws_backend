#!/bin/bash

rm -rf build

mkdir build

cd build

cmake -DCMAKE_BUILD_TYPE=Release ..

make

cp sqlite-demo ..

cd ..

rm -rf build

