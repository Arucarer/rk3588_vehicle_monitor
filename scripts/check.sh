#!/bin/sh
set -eu
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
(cd build && ctest --output-on-failure)
