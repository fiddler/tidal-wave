# Changelog

## 2026-11-17

### Added

- Backspace or Delete removes the selected tracks from a playlist, Tidal or
  local. More than one track asks first.

### Fixed

- "Remove from playlist" on a selected row removes the whole selection, not
  only the row under the cursor.
- Back returns a playlist to where it was scrolled instead of the top.
- Refreshing an offline playlist no longer jumps the list to the top.

## 2026-10-13

Most of this release follows upstream
[0.4.0](https://github.com/immineal/tidal-wave/releases/tag/v0.4.0) by
[immineal](https://github.com/immineal). The two trees no longer merge, so each
item was written again here; the upstream commit that did it first is named in
brackets. Items without one are this fork's own.

### Added

- Search loads more results as you scroll, one tab at a time, and keeps your
  recent searches (`5ff0832`).
- Track credits: who played, wrote and produced, from a track's menu or Now
  Playing. Lyrics can fill the page in large type (`a8137a8`, `398f283`).
- A track's radio opens as a mix that can be saved and unsaved. Saved mixes
  show in Home and the collection (`39b1465`, `024bb82`, `aad211e`).
- Every credited artist is its own link, on track rows, album headers, the
  player bar and Now Playing (`d591427`). They take Tab and Return.
- Artist pages list singles, EPs and compilations beside albums (`7926c18`).
- Queue rows and sidebar playlists travel to their new place when reordered
  (`d591427`).
- Animations stop when macOS "Reduce motion" is on. Read at launch
  (`5ef177d`).
- Context menus carry drawn icons instead of emoji (`e972e24`, `125ad11`).
- The back button walks a history of pages instead of one fixed parent.
- An album Tidal has delisted says so instead of loading forever.

### Changed

- Saved albums and artists come newest first.
- Sidebar playlists sort by the later of last played and date saved, so a
  playlist you have just saved shows at the top.
- The queue panel updates in place. A track change no longer rebuilds every
  row or jumps the list to the top (`7bdd1e2`, `b3d8dca`).

### Fixed

- Renaming a playlist reaches the server; it was only changed on screen and
  lost on the next visit (`5a80bda`). The description is loaded before the
  save, because the endpoint replaces both fields.
- Artist, radio and mix pages say when loading failed and offer a retry,
  instead of showing an empty page (`df5ba7e`).
- A stream or request that stops answering is given up on after 15 seconds
  (`3bc0ddc`).
- Search no longer says "No results" before the answer arrives, and a failed
  search clears the old results instead of leaving them under the new query
  (`6cfc474`).
- A queue edit near the end of a track no longer plays the track that had
  been preloaded before the edit.
- Launch no longer spends 74 ms looking for a font that is not installed
  (`0b93212`).
- A newly saved album, artist or mix shows on Home without a restart.
- Downloads find ffmpeg when the app is started from Finder or the Dock.

### Not taken

- Mixes in search results (`5ff0832`). Tidal's search returned no mixes on
  any query tried on 2026-10-09, so the tab stays hidden unless some arrive.

## 2026-09-27

### Added

- Albums ripped to one file with a CUE sheet import as separate tracks. The
  sheet is found beside the file (album.cue or album.flac.cue), dropped with
  it, or embedded in the FLAC's tags; a FLAC's chapter list works too. Each
  track plays its slice of the file, nothing is split on disk. Dropping the
  .cue alone imports the audio it describes, even when the sheet still names
  the .wav it was ripped to. An album already imported whole is split when
  imported again with its .cue beside it; importing a CUE album again also
  restores tracks removed from the library.
- Local tracks can be removed from the library: right-click "Remove from
  library", or ⌫ / Delete on a focused row. A multi-selection goes at once.
  Only the library entry is removed, never the file.

### Fixed

- Removing a local track no longer deletes the folder's cover image. The
  cover column pointed at the user's own cover.jpg, and removal deleted it.
- A local track's right-click menu no longer ends in two stray dividers. They
  belonged to Tidal-only items that were already hidden.

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
