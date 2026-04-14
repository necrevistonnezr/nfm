# Changelog

All notable changes to nfm are documented here.

---

## [1.0.2] — 2026-04-14

### Added
- **Filter-as-you-type in file browser** — press `/` to open the filter bar and
  start typing; the listing narrows in real-time to entries whose names contain
  the typed substring (case-insensitive, UTF-8-aware: typing `ün` finds
  `München`, typing `3` finds `123` and `367`)
- Filter bar shows match count while active (`/foo  (3 matches)`)
- `ESC` while typing closes the bar but keeps the filter; `ESC` again (or
  backspace on an empty bar) clears the filter entirely
- Arrow keys work normally during filtering so you can navigate while searching

---

## [1.0.1] — 2026-04-14

### Fixed
- **Garbled characters on macOS and Linux** — `setlocale(LC_ALL, "")` is now
  called before `initscr()` so ncurses uses UTF-8; this fixes garbage output
  for any multi-byte terminal characters
- **Umlaut / special-character filenames on macOS** — same locale fix; NFD-
  composed filenames (ä ö ü etc.) now display correctly in the browser
- Replaced raw Unicode symbols in footer/menu strings with ASCII equivalents
  as additional fallback for non-UTF-8 terminals

### Added
- **Media-only browser filter** (`m` key) — hides non-processable files
  (`.docx`, `.txt`, executables …) so only video, audio and image files plus
  directories are shown; `[media]` indicator appears in the browser title bar
  when the filter is active

---

## [1.0.0] — 2026-04-14

### Added
- Initial release
- ncurses file browser starting in the current working directory
- Real-time ffprobe info panel (codec, resolution, fps, bitrate, duration)
- Colour-coded preset menu — Video (cyan), Audio (yellow), Special (green)
- Presets loaded from editable `~/.config/nfm/presets/*.preset` text files
- Built-in presets: Plex 1080p, Plex 4K, 720p x265, Shrink Video,
  HQ AAC 256k, HQ MP3 320k, Extract Audio, GIF→Video, Video→GIF
- File-size savings estimate popup before "Shrink Video"
- Custom encoding form: codec, CRF, speed, resolution, container, audio
- Real-time encoding progress: fps, speed, ETA, bitrate, output size, log
- Hardware-capability detection: NVENC, VAAPI, VideoToolbox (macOS), QSV
- ffmpeg/ffprobe auto-detection with install offer (`apt` / `brew`)
- Hidden-file toggle (`.` key)
- Safe handling of spaces and special characters in file paths (exec, not shell)
- `make install` + `make deb` build targets
- `install.sh` for one-command setup on Ubuntu and macOS
