#!/bin/sh
# Builds web/lust.html: the menu sources compiled to WebAssembly (wasm32-wasi, zig c++ from `pip install ziglang`),
# gzip-compressed and embedded as base64 into a single self-contained HTML page (WebGL2 renderer in shell.html).
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
"$HERE/tools/fetch_velocity.sh"
B=$HERE/build/web; mkdir -p "$B"
ZIG=${ZIG:-"python3 -m ziglang"}
# model3d: the glove skin blob (.incbin) is only used by the inventory agent, not here
python3 - "$HERE/build/velocity/model3d.cpp" "$B/model3d_web.cpp" <<'PY'
import sys, re
s = open(sys.argv[1]).read()
a = s.index('asm(\n#ifdef _WIN32'); b = s.index('.text\\n");', a) + len('.text\\n");')
s = s[:a] + 'extern "C" { alignas(16) const unsigned char AGG_TEXBLOB[16] = { 0 }; }' + s[b:]
open(sys.argv[2], 'w').write(s)
PY
FLAGS="-target wasm32-wasi -O2 -std=c++17 -w -fno-exceptions -DLUST_WEB -DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS -include $HERE/web/compat_threads.h -I$HERE/src -I$HERE/third_party/imgui -I$HERE/build/velocity"
compile() { [ -f "$2" ] && [ "$2" -nt "$1" ] || $ZIG c++ $FLAGS -c "$1" -o "$2"; }
compile "$B/model3d_web.cpp" "$B/model3d.o" &
for f in imgui imgui_draw imgui_tables imgui_widgets; do compile "$HERE/third_party/imgui/$f.cpp" "$B/$f.o" & done
for f in draw icons ui preview persist menu pages; do $ZIG c++ $FLAGS -c "$HERE/src/astral/$f.cpp" -o "$B/a_$f.o" & done
$ZIG c++ $FLAGS -c "$HERE/web/web_main.cpp" -o "$B/web_main.o" &
wait
$ZIG c++ -target wasm32-wasi -O2 -mexec-model=reactor -s -o "$B/lust.wasm" "$B"/*.o
echo "wasm: $(du -h "$B/lust.wasm" | cut -f1)"
gzip -9 -c "$B/lust.wasm" > "$B/lust.wasm.gz"
python3 - "$HERE/web/shell.html" "$B/lust.wasm.gz" "$HERE/web/lust.html" <<'PY'
import sys, base64
shell = open(sys.argv[1]).read()
data = base64.b64encode(open(sys.argv[2], 'rb').read()).decode()
open(sys.argv[3], 'w').write(shell.replace('__WASM_GZ_BASE64__', data))
PY
echo "html: $HERE/web/lust.html ($(du -h "$HERE/web/lust.html" | cut -f1))"
