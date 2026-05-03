#!/usr/bin/env bash

# set -euo pipefail

mkdir -p bin

for file in bin/*.exe; do
    deps=$(ldd "$file" | awk '/=>/ {print $3}' | grep -i "^/$MSYSTEM/" | sort -u)
    if [ -n "$deps" ]; then
        cp -uv $deps bin/
    fi
done
