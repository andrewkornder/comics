#!/usr/bin/env bash

set -euo pipefail

mkdir -p bin
cp $(ldd bin/reader | awk '/=>/ {print $3}' | grep -i "/$MSYSTEM/" | sort -u) bin/
