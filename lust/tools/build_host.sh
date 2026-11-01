#!/bin/sh
# Builds the desktop screenshot host (tools/host_linux.cpp) into build/host/.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
"$HERE/tools/fetch_velocity.sh"
B=$HERE/build/host; mkdir -p $B
I="-I$HERE/src -I$HERE/third_party/imgui -I$HERE/build/velocity"
CXX="g++ -O2 -std=c++17 -w -DLUST_TOUCH $I"
[ -f $B/model3d.o ] || $CXX -c $HERE/build/velocity/model3d.cpp -o $B/model3d.o &
for f in imgui imgui_draw imgui_tables imgui_widgets; do [ -f $B/$f.o ] || $CXX -c $HERE/third_party/imgui/$f.cpp -o $B/$f.o & done
[ -f $B/gl3.o ] || $CXX -c $HERE/third_party/imgui/backends/imgui_impl_opengl3.cpp -o $B/gl3.o &
for f in draw icons ui preview persist menu pages; do $CXX -c $HERE/src/astral/$f.cpp -o $B/a_$f.o & done
$CXX -c $HERE/tools/host_linux.cpp -o $B/host.o &
wait
g++ -o $B/host_linux $B/*.o /usr/lib/x86_64-linux-gnu/libGL.so.1 -lX11 -ldl -lpthread
echo "built $B/host_linux"
