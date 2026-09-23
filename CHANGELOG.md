# Changelog

## 2026-09-23

### Added

- Albums can be dragged onto a playlist in the sidebar, from an artist's
  discography, search results, the home rows and the album collection. The
  album's tracks are fetched only when it is dropped.
- Adding tracks or an album to a playlist by drag reports progress and failures
  in the corner indicator instead of silently.
- A drop that cannot be kept says why instead of looking inert. Local files
  dropped on a Tidal playlist, or Tidal tracks dropped on a local playlist, tint
  the row red and explain the mismatch; dropping audio files while a Tidal
  playlist is open says the tracks are going to the local library.
- Local Files can be sorted by album artist, album, title, length or date
  added. The choice is remembered. Every order still reads an album in its own
  running order rather than alphabetically by song title.

### Fixed

- Importing local files works when the app is launched from Finder or the Dock.
  ffprobe was looked up once on the inherited PATH, which there holds only
  /usr/bin:/bin:/usr/sbin:/sbin — so the same import succeeded from a terminal
  and failed with "not found" from the Dock. Known install prefixes are searched
  too, and the lookup is repeated before each import, so installing ffmpeg takes
  effect without a restart.
- Local library errors and import results are reported wherever you are. They
  were announced only on the Local Files page, so an import started anywhere
  else — dropping files on a playlist, say — failed in silence.
- A split or compilation album stays in one block in Local Files. Sorting led
  with the track artist, so an album whose tracks each name a different
  performer was scattered across the list. Tracks now carry an album artist,
  read from the tag on import and filled in for existing libraries from the
  album's own tracks.
- A time zone change no longer signs you out. The token expiry was saved in
  local time without an offset, so after a change an expired token read as
  valid, Tidal refused it, and the app dropped to the login page while
  holding a good refresh token. The expiry is now saved in UTC, and a refused
  session check tries one refresh before signing out.

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
