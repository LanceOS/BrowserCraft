#!/usr/bin/env bash
# Convert all 8-bit RGBA block textures to 16-bit per channel (64-bit RGBA).
#
# Usage: bash scripts/convert-textures-to-16bit.sh
#
# This uses ImageMagick to losslessly upscale 8-bit channels to 16-bit
# via the standard (value * 257) mapping, identical to what stbi_load_16
# does at load time.  The visual output is identical; the file now stores
# the full 16-bit range so editing tools can preserve higher precision.

set -euo pipefail

TEXTURE_DIR="assets/textures"
COUNT=0

for png in "$TEXTURE_DIR"/*.png; do
  # Read the raw bit depth from the PNG IHDR chunk (more reliable than
  # ImageMagick's identify, which may report palette depth incorrectly).
  depth=$(python3 -c "
import struct, sys
with open(sys.argv[1], 'rb') as f:
    data = f.read()
idx = data.find(b'IHDR')
if idx >= 0:
    print(data[idx+12])
else:
    print(0)
" "$png" 2>/dev/null)

  if [ "$depth" = "16" ]; then
    echo "SKIP  $png — already 16-bit"
    continue
  fi

  # Convert to 16-bit per channel RGBA (PNG64 = 16-bit per sample RGBA)
  # -type TrueColorAlpha forces expansion from palette to truecolor RGBA,
  # which is required because PNG's palette mode doesn't support 16-bit.
  echo "CONV  $png  (was ${depth}-bit)"
  magick "$png" -depth 16 -type TrueColorAlpha PNG64:"$png"
  ((COUNT++))
done

echo ""
echo "Done. Converted $COUNT texture(s) to 16-bit per channel."
