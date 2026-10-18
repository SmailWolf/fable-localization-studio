#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p bin .build
for name in babel fonts jobs tests main; do
 if [ ! -f ".build/$name.o" ] || [ "src/$name.cpp" -nt ".build/$name.o" ] || [ src/core.hpp -nt ".build/$name.o" ]; then
  c++ -std=c++20 -O2 -fPIC -pthread -Wall -Wextra -Wno-deprecated-declarations $(pkg-config --cflags Qt6Widgets Qt6Network Qt6Sql zlib) -c "src/$name.cpp" -o ".build/$name.o"
 fi
done
c++ -pie -pthread .build/babel.o .build/fonts.o .build/jobs.o .build/tests.o .build/main.o $(pkg-config --libs Qt6Widgets Qt6Network Qt6Sql zlib) -o bin/fable-localization-studio
