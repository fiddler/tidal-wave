#!/bin/bash

# Capture playback and CoreAudio evidence without restarting either process.
# macOS ships Bash 3.2, so keep this script compatible with it.

set -u

use_sudo=0
if [ "${1:-}" = "--sudo-coreaudio" ]; then
    use_sudo=1
elif [ "$#" -ne 0 ]; then
    echo "usage: $0 [--sudo-coreaudio]" >&2
    exit 2
fi

script_dir=$(cd "$(dirname "$0")" && pwd)
repo_dir=$(cd "$script_dir/.." && pwd)
stamp=$(date "+%Y%m%d-%H%M%S")
out_dir="$repo_dir/diagnostics/audio-stall-$stamp"
mkdir -p "$out_dir"

app_pids_file="$out_dir/tidal-wave-pids.txt"
core_pids_file="$out_dir/coreaudiod-pids.txt"
/usr/bin/pgrep -x "Tidal Wave" >"$app_pids_file" \
    2>"$out_dir/tidal-wave-pgrep.stderr.txt" || true
/usr/bin/pgrep -x coreaudiod >"$core_pids_file" \
    2>"$out_dir/coreaudiod-pgrep.stderr.txt" || true

{
    echo "Playback stall diagnostic capture. No process was restarted or killed."
    echo "Captured at: $(date "+%Y-%m-%dT%H:%M:%S%z")"
    echo "Repository: $repo_dir"
    echo "Tidal Wave PIDs: $(tr '\n' ' ' <"$app_pids_file")"
    echo "coreaudiod PIDs: $(tr '\n' ' ' <"$core_pids_file")"
} >"$out_dir/README.txt"

{
    date "+date=%Y-%m-%dT%H:%M:%S%z"
    /usr/bin/sw_vers
    /usr/bin/uname -a
    /usr/bin/uptime
    echo "shell=${SHELL:-unknown}"
} >"$out_dir/system.txt" 2>&1

{
    echo "pid ppid user etime state command"
    for pid_file in "$app_pids_file" "$core_pids_file"; do
        while IFS= read -r pid; do
            [ -n "$pid" ] || continue
            case "$pid" in *[!0-9]*) continue ;; esac
            /bin/ps -p "$pid" -o pid=,ppid=,user=,etime=,state=,command=
        done <"$pid_file"
    done
} >"$out_dir/processes.txt" 2>&1

/usr/sbin/system_profiler SPAudioDataType -detailLevel mini \
    >"$out_dir/audio-devices.txt" 2>&1
/usr/sbin/ioreg -r -c IOAudioDevice -l \
    >"$out_dir/audio-ioreg.txt" 2>&1
/bin/launchctl print system/com.apple.audio.coreaudiod \
    >"$out_dir/coreaudiod-launchd.txt" 2>&1 || true

{
    for hal_dir in /Library/Audio/Plug-Ins/HAL /System/Library/Audio/Plug-Ins/HAL; do
        echo "$hal_dir"
        if [ ! -d "$hal_dir" ]; then
            echo "  missing"
            continue
        fi
        for driver in "$hal_dir"/*.driver; do
            [ -e "$driver" ] || continue
            identifier=$(/usr/bin/defaults read "$driver/Contents/Info" CFBundleIdentifier 2>/dev/null || echo unknown)
            version=$(/usr/bin/defaults read "$driver/Contents/Info" CFBundleShortVersionString 2>/dev/null || echo unknown)
            echo "  $(basename "$driver") id=$identifier version=$version"
        done
    done
} >"$out_dir/hal-drivers.txt" 2>&1

{
    for bundle in \
        "$repo_dir/build/Tidal Wave.app" \
        "$HOME/Applications/Tidal Wave.app" \
        "/Applications/Tidal Wave.app"; do
        [ -d "$bundle" ] || continue
        version=$(/usr/bin/defaults read "$bundle/Contents/Info" CFBundleShortVersionString 2>/dev/null || echo unknown)
        build=$(/usr/bin/defaults read "$bundle/Contents/Info" CFBundleVersion 2>/dev/null || echo unknown)
        echo "$bundle version=$version build=$build"
        if [ -x "$bundle/Contents/MacOS/Tidal Wave" ]; then
            /usr/bin/otool -L "$bundle/Contents/MacOS/Tidal Wave" | /usr/bin/grep -E 'mpv|QtCore' || true
        fi
    done
} >"$out_dir/app-builds.txt" 2>&1

/usr/bin/log show --last 15m --style compact --info --debug \
    --predicate 'process == "Tidal Wave" OR process == "coreaudiod"' \
    >"$out_dir/unified-log.txt" 2>&1 || true

flight_log="$HOME/Library/Logs/Tidal Wave/playback.jsonl"
if [ -f "$flight_log.1" ]; then
    /bin/cp "$flight_log.1" "$out_dir/playback.jsonl.1"
fi
if [ -f "$flight_log" ]; then
    /bin/cp "$flight_log" "$out_dir/playback.jsonl"
else
    echo "No playback flight recorder found at $flight_log" \
        >"$out_dir/playback-log-missing.txt"
fi

sudo_ready=0
if [ "$use_sudo" -eq 1 ] && [ -s "$core_pids_file" ]; then
    echo "Authorizing the coreaudiod stack sample with sudo..." >&2
    if /usr/bin/sudo -v; then
        sudo_ready=1
    else
        echo "sudo authorization failed; recording the unprivileged sample error instead" >&2
    fi
fi

while IFS= read -r app_pid; do
    [ -n "$app_pid" ] || continue
    case "$app_pid" in *[!0-9]*) continue ;; esac
    /usr/bin/sample "$app_pid" 5 -file "$out_dir/tidal-wave-sample-$app_pid.txt" \
        >"$out_dir/tidal-wave-sample-$app_pid.stderr.txt" 2>&1 || true
done <"$app_pids_file"

while IFS= read -r core_pid; do
    [ -n "$core_pid" ] || continue
    case "$core_pid" in *[!0-9]*) continue ;; esac
    if [ "$sudo_ready" -eq 1 ]; then
        /usr/bin/sudo -n /usr/bin/sample "$core_pid" 5 \
            -file "$out_dir/coreaudiod-sample-$core_pid.txt" \
            >"$out_dir/coreaudiod-sample-$core_pid.stderr.txt" 2>&1 || true
    else
        /usr/bin/sample "$core_pid" 5 \
            -file "$out_dir/coreaudiod-sample-$core_pid.txt" \
            >"$out_dir/coreaudiod-sample-$core_pid.stderr.txt" 2>&1 || true
    fi
done <"$core_pids_file"

echo "$out_dir"
