#!/usr/bin/env bash
set -euo pipefail

rm -rf res/fonts
mkdir -p res/fonts res/fonts/Noto_Sans

wget https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip -qO res/fonts/jetbrains.zip
unzip -d res/fonts/JetBrains-2.304 res/fonts/jetbrains.zip
rm res/fonts/jetbrains.zip

wget https://www.1001fonts.com/download/noto-sans.zip -qO res/fonts/notosans.zip
unzip -d res/fonts/Noto_Sans/static res/fonts/notosans.zip
rm res/fonts/notosans.zip


