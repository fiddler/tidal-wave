# Tidal Wave — working notes

Qt/QML desktop TIDAL client, C++ with CMake, playing through libmpv. This checkout is our fork:
`fork` is github.com/fiddler/tidal-wave branch **`extended`**, `origin` is upstream immineal and
`main` mirrors it. Push to `fork`, never `origin`. Conventional commits. What the fork adds on top
of upstream is listed at the top of `README.md`.

## Dev loop

`make -j8` in `build/`, then run `build/Tidal Wave.app/Contents/MacOS/Tidal Wave` directly. Quit
any running instance first, because the single-instance socket otherwise hands off to the old
binary: `osascript -e 'quit app "Tidal Wave"'`, then `pkill -x "Tidal Wave"` as a fallback.

Run it in the **foreground** from Bash with a 60s timeout, which backgrounds it and keeps it alive
for AppleScript tests. `run_in_background: true` also works once the bundle is clean. An app that
"dies within seconds" is almost always the Qt double-load below, not the backgrounding.

## Tests

`ctest --test-dir build` runs both suites, about seven seconds. `tst_api` feeds fixtures to the
static parsers in `TidalClient` and needs no network. `tests/run-qml-tests.py` runs the real QML
pages against stub `bridge` and `player` objects declared in each test file. A page under test has
no `Window.window.navigate`, so test what a click calls on the stubs, not where it navigates.

`ListView.itemAtIndex()` right after a model move still answers the old order; `wait()` past the
move transition before asserting on it.

Two traps. moc sees no test class in a file that contains a raw string literal, so fixtures in
`tests/cpp` use single quotes and `json()` swaps them. And a `mouseClick` on something that became
visible in the same tick lands at its old geometry: `waitForRendering(page)` first.

## The bundle traps

The build bundle is deployed **in place**. After a `deploy`, `build/Tidal Wave.app` carries
bundled Qt frameworks, so a later plain `make` binary loads Homebrew Qt *and* the bundled copy and
dies with "Could not load the Qt platform plugin cocoa", exit 134. Hand-deleting parts of the
bundle is no better: macdeployqt skips `Contents/Resources/qml` when it already exists, leaving
dangling symlinks into a deleted `Contents/PlugIns` and a `module "QtQuick.Controls" plugin not
found` failure. One cure for both: `rm -rf "build/Tidal Wave.app"`, then `make`, then `deploy`.

Install with `cmake --build build --target deploy` (macdeployqt plus dylibbundler, takes minutes,
verifies self-containment), then replace `~/Applications/Tidal Wave.app` with the build bundle.
Never deploy while the app is running from the build bundle.

**After installing, prove the user is testing the new binary:** `ps -Ao pid,lstart,args | grep
"Tidal Wave"` and check the start time is later than the install. `open` on a running app only
activates it, so an "it is still broken" report can be a stale process. That cost a full diagnosis
cycle on 2026-08-17.

## Driving the UI without a human

**Never fire a synthetic keystroke or click without re-checking `System Events → name of first
process whose frontmost is true` in the same step, immediately before the input.** Checking once
at the top of a script is not enough, focus moves between steps. While measuring CPU on 2026-08-24
a `keystroke " "` meant as play/pause landed in Jussi's browser several times, scrolling pages and
pausing video. A blind keystroke is not a no-op when it misses, it is input into whatever the user
is doing. The same guard protects `screencapture -R`, which otherwise grabs their screen.

Prefer a measurement that needs no input at all. Say up front when a test will open a helper app,
since a stray Calculator window reads as "are we hacked?".

What actually works: AppleScript through System Events, `click button 1 of window 1` to close the
window, `tell application "Tidal Wave" to activate` as a Dock click, `click menu item "Quit Tidal
Wave" of menu 1 of menu bar item 2 of menu bar 1` for ⌘Q (a synthetic `keystroke "q" using command
down` does not reach the app). `screencapture -x -R 900,0,1000,40 out.png` shows the menu bar.

Synthetic input into the Qt window is half-blind (2026-08-21): `cliclick` clicks reach MouseArea
(track rows), but a TapHandler (sidebar rows, nav items) needs `dc:`. Keyboard modifiers never
arrive at all, from cliclick or System Events, so cmd/shift-click multi-select cannot be driven.
F7-F12 are eaten by macOS. Plain digit keys work as temporary Shortcut hooks.

## Little Snitch

Every rebuild trips it ("The program has been modified", api.tidal.com). The alert ignores
synthetic clicks and keystrokes and exposes no accessibility window, so only the user can accept
it. Budget one ask per install and never touch "Disable Identity Check". Before the offline fix on
2026-08-21 a pending prompt also jammed the app on the login page, which is why testing looked
like the app was broken. See the workstation section in `../CLAUDE.md`.

## Debugging recipes

### Playback jam / CoreAudio

When Jussi says playback is jammed or asks to debug a playback jam, capture the live state first:

```bash
cd /Users/master/projects/tidal-wave
scripts/capture-audio-stall.sh
```

Do this while it is still jammed, before restarting Tidal Wave or running
`sudo killall coreaudiod`. The script changes no system state. It writes a timestamped directory
under `diagnostics/` with both process lists, the current audio device and HAL driver inventory,
15 minutes of Tidal Wave and `coreaudiod` unified logs, the app's playback flight recorder, and a
five-second stack sample of each accessible process. Run it with escalated read-only access if the
agent sandbox blocks process inspection or `/usr/bin/log`. If the `coreaudiod` sample reports a
permission error, ask for approval and rerun with:

```bash
scripts/capture-audio-stall.sh --sudo-coreaudio
```

Use a PTY for the sudo variant so its authorization prompt is visible.

Inspect the newest `diagnostics/audio-stall-*` directory and correlate its timestamps with:

`~/Library/Logs/Tidal Wave/playback.jsonl`

That JSON Lines log is always on, flushed after every record, rotated at 10 MiB, and keeps one
previous file as `playback.jsonl.1`. It records mpv lifecycle and selected verbose output,
CoreAudio output selection and format, buffering flags, position progress, errors, snapshots, and
audio-output reload results. Stream URLs, bearer values, token-like query parameters, and the macOS
username in paths are redacted.

The app detects two failure shapes: loading for 30 seconds and a nominally playing position frozen
for 10 seconds. It takes a cached-state snapshot and requests one asynchronous mpv `ao-reload`.
It does not restart `coreaudiod`. A second recovery is allowed only after position progress resumes
or a new source is loaded, which prevents a stuck output from entering a reload loop.

Recovery order after capture:

1. Check whether the automatic `ao-reload` restored position progress.
2. Restart Tidal Wave if needed.
3. Use `sudo killall coreaudiod` only last.

Known incident on 2026-08-28: restarting Tidal Wave did not reopen CoreAudio. Restarting
`coreaudiod` created a new daemon and the next Tidal Wave output `StartIO` succeeded. This locates
the failure at the mpv/CoreAudio boundary but does not prove whether the daemon, mpv, or a HAL
driver owned the deadlock. The recorder and paired stack samples were added to resolve that next
time.

### Crackle, not a jam

A different shape, and the recorder does not catch it. Crackling or clicking on **any** cmd-tab,
whichever apps the switch is between, audio from any source. The recorder only detects loading for
30 seconds and a frozen position for 10, so a glitch of a few milliseconds never trips it, and mpv
prints `Audio device underrun detected` only for gaps far larger. Expect a clean `playback.jsonl`
while the user hears it.

On 2026-09-12 `sudo killall coreaudiod` cleared it at once. Second time the fault has landed at that
boundary after 2026-08-28, so go there early rather than reading the app's logs for an hour.

Three things that look like leads and are not. Tidal Wave idles around 2.7% CPU with its real-time
audio thread barely used, so it is not starving its own callback. The activation path touches no
audio: `applicationStateChanged` re-shows the window (`Application.cpp:254`) and moves the position
poll from 1000ms to 200ms (`MpvAudio.cpp:178`). And a burst of 50 `creating DSPProcessor` /
`DSPProcessor destroyed` pairs in the `coreaudiod` log is a **track change**, not a switch — a new AO
open makes macOS re-enumerate the DSP chain of every audio-capable app on the machine.

The isolation test, which needs no synthetic input: `afplay` a sine tone. It has no window, so
nothing about app switching involves it. If the tone crackles too, the app is a bystander.

**libmpv internals** (filter chain, AO format): the permanent playback recorder already requests
verbose mpv messages and persists selected `ao`, `cplayer`, `cache`, `demux`, `ffmpeg`, and `stream`
prefixes. Extend `keepMpvLogPrefix()` in `MpvAudio.cpp` temporarily if another prefix is needed;
do not print raw stream URLs or authorization data.

**Idle CPU** (2026-08-24): the whole idle cost is the Qt Quick render loop, and that loop runs
only while some `QAbstractAnimation` is running. `QSG_RENDER_TIMING=1` on the installed binary
prints one `syncAndRender: start` line per frame, so `grep -c` over a fixed interval gives
frames/second, a far better signal than %CPU. `QT_LOGGING_RULES="qt.quick.dirty=true"` printing an
*empty* `updateDirtyNodes()` every frame means an `Animator` is stuck running on the render thread
without dirtying nodes. `QSG_INFO=1` reports the swapchain MSAA sample count. Two traps: `sample`
counts blocked threads, so its per-thread totals are not CPU, and a window fully covered by
another app renders zero frames, so measure only while the window is visible.

**Offline and reconnect paths:** `sandbox-exec -p '(version 1)(allow default)(deny network*)'
"build/Tidal Wave.app/Contents/MacOS/Tidal Wave"` gives a clean offline start with no firewall
prompt, but it can never recover. To test reconnection, point `kApiBase` at
`http://127.0.0.1:8899/v1/` in a throwaway build and run with `HOME=/tmp/twtesthome` (copy
`~/.config/tidal-wave/credentials.json` there). Start with nothing listening, then bring up a
small Python stub for `sessions` and `users/{id}`. The isolated HOME keeps the stub's fake user id
out of the real credentials file.

## Tidal API facts (probed live 2026-10-09, PKCE token)

- **Search returns no mixes.** `v1/search` answers `tracks`, `albums`, `artists`, `playlists`,
  `videos`, `topHit` and nothing else, on four queries including "my mix". `v2/search` has no
  mixes key either. Upstream's "finds mixes" (`5ff0832`) cites only stub tests, no live reply. The
  Mixes search tab therefore hides itself unless a reply carries some (`SearchPage.qml`).
- **`types` on `v1/search` is ignored.** `types=MIXES` and `types=ALBUMS` both return all four
  kinds. A page of one tab downloads the other three.
- `artists/<id>/albums` unfiltered is albums only. `filter=EPSANDSINGLES` and
  `filter=COMPILATIONS` are separate, disjoint lists.
- Favourites take `order=DATE&orderDirection=DESC`. Without it artists come back by name.
- A track's radio is a mix: `tracks/<id>` carries `mixes.TRACK_MIX`, and `pages/mix?mixId=`
  returns a `MIX_HEADER` and a `TRACK_LIST`. Saved mixes live at `v2/favorites/mixes`.
- `tracks/<id>/credits?includeContributors=true` is a bare JSON array of `{type, contributors}`.
- A delisted album answers 404 subStatus 2001 while favourites still list it.
- `playbackinfopostpaywall` serves this client FLAC (93 of 93 streams in `playback.jsonl`,
  28 Aug to 22 Sep). Upstream's device-code client gets 320 kbps AAC from it.

## QML conventions

- **Durations go through `Theme.dur(ms)`.** It returns 0 when the system "reduce motion" setting
  is on (`app.reducedMotion`, read once at launch, copied into `Theme` by `Main.qml`). Spinners
  are exempt: they report progress. Decorative loops add `&& !Theme.reducedMotion` to `running`.
- **Name no font family that may be missing.** The first family Qt cannot resolve costs a scan of
  every font alias, 74 ms per launch here. `Application.cpp` filters its list through
  `QFontDatabase::hasFamily`; QML uses `app.monospaceFamily`, never `"monospace"`.
- **A list animates a reorder only if its model is told about the move.** Assigning a new JS
  array is a reset. The queue panel reads `player.queueModel` (`QueueModel::sync` diffs each edit
  into a move, removal or insertion); the sidebar syncs its `ListModel` by uuid.
- **Context menu rows are `IconMenuItem`**, with a `VectorIcon` name, not an emoji in the label.

## Misc

- User settings: `~/Library/Preferences/com.tidalwave.Tidal Wave.plist` (org "TidalWave").
  QSettings keys like `eq/enabled` appear as `eq.enabled`.
- External review pattern applies: codex plus grok on the feature commit, fixes as a follow-up
  commit referencing the reviewed SHA, push only after the user's OK.
