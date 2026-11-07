#!/usr/bin/env bash
# Installs LibrEQ for the current user. Run without sudo.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
target="${HOME}/.vst3"
mkdir -p "$target"
rm -rf "$target/LibrEQ.vst3"
cp -R "$here/LibrEQ.vst3" "$target/"
chmod 755 "$target/LibrEQ.vst3/Contents/x86_64-linux/LibrEQ.so"
echo "LibrEQ installed in $target. Rescan plug-ins in your DAW."
