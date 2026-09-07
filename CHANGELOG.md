# Changelog

## 2026-09-07

### Fixed

- Downloaded playlists show their cached tracks immediately while refreshing
  from Tidal, even when an earlier playlist request is stalled.
- Playlist loads stop showing the spinner after 15 seconds and offer Retry.
  Cached tracks remain available if the refresh fails or times out.
- Responses from an older playlist load or retry no longer replace the current
  playlist's tracks or clear its loading state.
- Playback position and duration queries run asynchronously so a stalled audio
  engine does not block the UI while it waits for those values.
- Playback that stops progressing for 10 seconds outside buffering, or stays
  loading for 30 seconds, captures diagnostic state and attempts to reopen mpv's
  audio output. Recovery is attempted once until playback progresses or another
  source loads; the app does not restart macOS CoreAudio.

### Added

- Persistent playback diagnostics record audio-output state, buffering, errors,
  and recovery attempts. Logs redact stream URLs and credentials, rotate at
  10 MiB, and retain one previous file.
- A macOS diagnostic capture script collects playback and CoreAudio logs,
  audio-device details, and process stack samples without restarting either
  process. Capture output is excluded from Git.
- Developer notes document build, installation, UI automation, and playback-jam
  investigation procedures.
