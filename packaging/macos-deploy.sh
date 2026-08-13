#!/bin/sh
# Turns the linked .app into one that runs without Homebrew.
#
# Run through CMake: cmake --build build --target deploy
#
# macdeployqt handles Qt (frameworks plus the QML modules a QtQuick app needs)
# but knows nothing about libmpv, whose dependency tree is ~56 dylibs. And
# pointing dylibbundler at the executable alone is not enough: the libraries it
# copies in, and the Qt frameworks macdeployqt copied, carry their own Homebrew
# references. Every Mach-O in the bundle has to be passed in, or the result
# still loads from /opt/homebrew and only works on a machine that has it.
set -e

APP="$1"
MACDEPLOYQT="$2"
DYLIBBUNDLER="$3"
QMLDIR="$4"

[ -d "$APP" ] || { echo "no bundle at $APP" >&2; exit 1; }

echo "==> macdeployqt"
"$MACDEPLOYQT" "$APP" -qmldir="$QMLDIR"

# macdeployqt copies the Qt Multimedia backend on sight. Playback is libmpv's
# job now, nothing links Qt Multimedia and no QML imports it, so it is dead
# weight that also drags its own dependencies into the bundling pass below.
rm -rf "$APP/Contents/PlugIns/multimedia"

echo "==> collecting Mach-O files in the bundle"
set --
while IFS= read -r f; do
    case "$(file -b "$f")" in
        *Mach-O*) set -- "$@" -x "$f" ;;
    esac
done <<EOF
$(find "$APP/Contents" -type f \( -perm -u+x -o -name '*.dylib' -o -name '*.so' \))
EOF

echo "==> dylibbundler over $(( $# / 2 )) binaries"
"$DYLIBBUNDLER" -cd -b -ns -of \
    -d "$APP/Contents/Frameworks" \
    -p "@executable_path/../Frameworks" \
    "$@"

# macdeployqt and dylibbundler each add @executable_path/../Frameworks to the
# executable's rpath, and dyld refuses a binary with a duplicate LC_RPATH:
#   Library not loaded: @executable_path/../Frameworks/libmpv.2.dylib
#   Reason: duplicate LC_RPATH '@executable_path/../Frameworks/'
# Drop the extra copies, keeping one.
# Every Mach-O, not just the executable: the duplicate showed up inside
# libmpv.2.dylib, which is what actually failed to load.
echo "==> removing duplicate rpaths"
find "$APP/Contents" -type f \( -perm -u+x -o -name '*.dylib' -o -name '*.so' \) |
while IFS= read -r f; do
    case "$(file -b "$f")" in *Mach-O*) ;; *) continue ;; esac
    otool -l "$f" | awk '/LC_RPATH/{r=1} r&&/ path /{print $2; r=0}' | sort | uniq -c |
    while read -r count path; do
        while [ "$count" -gt 1 ]; do
            install_name_tool -delete_rpath "$path" "$f" 2>/dev/null || true
            count=$((count - 1))
        done
    done
done

# install_name_tool invalidates code signatures, and on Apple silicon the loader
# refuses an invalid one. Ad-hoc re-signing is the minimum that makes it load.
echo "==> re-signing"
codesign --force --deep --sign - "$APP"

echo "==> verifying nothing still points at Homebrew"
LEFT=$(find "$APP/Contents" -type f \( -perm -u+x -o -name '*.dylib' \) -exec sh -c '
    for f do otool -L "$f" 2>/dev/null | tail -n +2 | grep -q "/opt/homebrew" && echo "$f"; done
' sh {} + | wc -l | tr -d ' ')
if [ "$LEFT" = "0" ]; then
    echo "OK: bundle is self-contained"
else
    echo "WARNING: $LEFT binaries still reference /opt/homebrew" >&2
    exit 1
fi
