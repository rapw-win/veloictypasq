#!/bin/sh
# Extracts the velocitypasq files the menu uses (3D agent renderer, its model data, ESP fonts and weapon icons)
# from the source archive at the repository root into build/velocity/.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
ZIP="$HERE/../Anesthysia_SO2_v19_9_source.zip"
OUT="$HERE/build/velocity"
[ -f "$OUT/.done" ] && exit 0
mkdir -p "$OUT"
for f in model3d.cpp model3d.h model_data.h anim_data.h model_ct.h model_t.h model_fp.h sero_tex.h model_gloves.h stb_image.h esp_assets.h wicons.h agg_tex.bin; do
  unzip -o -j -q "$ZIP" "source/$f" -d "$OUT"
done
touch "$OUT/.done"
echo "velocitypasq sources -> $OUT"
