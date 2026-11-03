#!/bin/bash -e

cd "$(dirname "$0")"
makepkg -sf
rm -rf pkg src
