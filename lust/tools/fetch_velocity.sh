#!/bin/sh
# Extracts the velocitypasq files the menu uses (3D agent renderer, its model data, ESP fonts and weapon icons)
# from the source archive at the repository root into build/velocity/.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
ZIP="$HERE/../Anesthysia_SO2_v19_9_source.zip"
OUT="$HERE/build/velocity"
if [ ! -f "$OUT/.done" ]; then
mkdir -p "$OUT"
for f in model3d.cpp model3d.h model_data.h anim_data.h model_ct.h model_t.h model_fp.h sero_tex.h model_gloves.h stb_image.h esp_assets.h wicons.h agg_tex.bin; do
  unzip -o -j -q "$ZIP" "source/$f" -d "$OUT"
done
touch "$OUT/.done"
echo "velocitypasq sources -> $OUT"
fi
# lust tweak: model3d's parallel rasteriser leaves one core free, so the menu / GL thread never waits for it
python3 - "$OUT/model3d.cpp" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
old = 'std::min(8, (int)std::thread::hardware_concurrency())'
new = 'std::min(8, (int)std::thread::hardware_concurrency() - 1) /* lust: keep a core for the UI */'
if old in s:
    open(p, 'w').write(s.replace(old, new))
PY
