#!/bin/sh
# Builds chess3ds.3dsx and chess3ds.cia inside the devkitPro Docker image.
# makerom and bannertool are compiled from source on first run (cached in .tools/).
set -e
cd "$(dirname "$0")"

docker run --rm -v "$PWD":/project -w /project devkitpro/devkitarm:latest sh -c '
set -e
export PATH="/project/.tools/bin:$PATH"
if ! command -v makerom >/dev/null || ! command -v bannertool >/dev/null; then
	command -v git >/dev/null && command -v g++ >/dev/null || (apt-get update -qq && apt-get install -y -qq git build-essential >/dev/null)
	mkdir -p .tools/bin .tools/src
	[ -d .tools/src/Project_CTR ] || git clone --depth 1 https://github.com/3DSGuy/Project_CTR.git .tools/src/Project_CTR
	(cd .tools/src/Project_CTR/makerom && make deps && make)
	cp .tools/src/Project_CTR/makerom/bin/makerom .tools/bin/
	[ -d .tools/src/bannertool ] || git clone --depth 1 --recursive https://github.com/diasurgical/bannertool.git .tools/src/bannertool
	(cd .tools/src/bannertool && make)
	cp .tools/src/bannertool/output/linux-x86_64/bannertool .tools/bin/
fi
make cia
'
