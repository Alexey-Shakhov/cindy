cd build/offline-output
magick -size 1920:1080 -depth 8 bgra:normal.bin normal.png
magick -size 1920:1080 -depth 8 bgra:color.bin color.png
magick -size 1920:1080 -depth 32 -define quantum:format=floating-point gray:depth.bin -auto-level depth.png
