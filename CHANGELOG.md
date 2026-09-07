# Changelog

## 2026-09-07

### Fixed

- Downloaded playlists show their cached tracks immediately while refreshing
  from Tidal, even when an earlier playlist request is stalled.
- Playlist loads stop showing the spinner after 15 seconds and offer Retry.
  Cached tracks remain available if the refresh fails or times out.
- Responses from an older playlist load or retry no longer replace the current
  playlist's tracks or clear its loading state.
