# nfm — Flexible ncurses Video Converter

A keyboard-driven terminal frontend for **ffmpeg**, built with ncurses.  
Inspired by [nano-ffmpeg](https://github.com/dgr8akki/nano-ffmpeg), rewritten in C.

```
┌─ nfm 1.0.3 ─────────────────────────────── /home/user/Videos ─┐
│ Files                   │ Info                                  │
│──────────────────────── │ ──────────────────────────────────────│
│ /  ..                   │ movie.mkv                             │
│ V  movie.mkv    2.1 GB  │                                       │
│ V  clip.mp4   340 MB    │ Container:   matroska,webm            │
│ A  song.flac   45 MB    │ Duration:    1:52:30                  │
│ I  banner.gif   2 MB    │ Size:        2.1 GB                   │
│                         │ Bitrate:     2.5 Mbps                 │
│                         │                                       │
│                         │ ─ Video ─                             │
│                         │ Codec:       h264                     │
│                         │ Resolution:  1920x1080                │
│                         │ Frame rate:  23.98 fps                │
│                         │                                       │
│                         │ ─ Audio ─                             │
│                         │ Codec:       aac                      │
│                         │ Sample rate: 48000 Hz                 │
│                         │ Channels:    2                        │
└─────────────────────────┴───────────────────────────────────────┘
 [↑↓] Navigate  [Enter] Open  [←/Back] Up  [.] Hidden  [?] Help  [q] Quit
```

## Features

- **File browser** starting in the current working directory
- **Filter-as-you-type** — press `/` to narrow the listing in real time (UTF-8-aware, case-insensitive)
- **Media-only view** — `m` hides non-media files; only video, audio, images and directories shown
- **ffprobe integration** — codec, resolution, bitrate, duration shown in real time
- **Colour-coded presets** — Video (cyan), Audio (yellow), Special (green)
- **Hardware detection** — VideoToolbox (macOS), NVENC, VAAPI, QSV
- **Real-time progress** — speed, ETA, FPS, bitrate, live ffmpeg log
- **Savings estimate** — shows expected file-size reduction before encoding
- **Custom settings** — choose codec, CRF, speed, resolution, container interactively
- **User-editable presets** — plain `.preset` text files in `~/.config/nfm/presets/`
- **Special characters / spaces** in file paths handled safely (no shell injection)
- Runs on **Ubuntu 24.04** and **macOS**

## Built-in Presets

| Name | Type | Description |
|---|---|---|
| Plex Compatible 1080p | Video | H.264, CRF 18, 1080p, fast-start |
| Plex Compatible 4K | Video | H.264, CRF 18, 4K, fast-start |
| 720p x265 Medium Quality | Video | H.265/HEVC, CRF 23, 720p |
| Shrink Video | Video | H.265, keep resolution — shows savings estimate |
| HQ AAC 256k | Audio | AAC, 256 kbps |
| HQ MP3 320k | Audio | MP3 (libmp3lame), 320 kbps CBR |
| Extract Audio (copy) | Audio | Lossless stream copy |
| GIF to Video | Special | GIF → MP4 |
| Video to GIF | Special | Video → animated GIF (15 fps, 480 px, max 30 s) |

## Installation

### Quick install (Ubuntu / macOS)

```bash
git clone https://github.com/necrevistonnezr/nfm
cd nfm
bash install.sh
```

### Manual

```bash
# Ubuntu 24.04
sudo apt-get install gcc make libncurses-dev ffmpeg
make
sudo make install          # installs to /usr/local

# macOS (Homebrew)
brew install ncurses ffmpeg
make
sudo make install
```

### Debian .deb package (Ubuntu / Debian)

```bash
make deb
sudo dpkg -i build/nfm_1.0.3_amd64.deb
```

## Usage

```bash
nfm              # start in current directory
nfm /path/to/videos
```

### Key bindings

| Key | Action |
|---|---|
| `↑` / `↓` / `k` / `j` | Navigate files |
| `Enter` | Enter directory / open file menu |
| `←` / `Backspace` | Go up one directory |
| `PgUp` / `PgDn` | Scroll quickly |
| `/` | Open filter bar — type to narrow listing in real time |
| `ESC` (in filter) | Close bar, keep filter active |
| `ESC` (filter inactive) | Clear filter entirely |
| `F2` / `e` | Rename file or folder |
| `.` | Toggle hidden files |
| `m` | Toggle media-only view |
| `r` | Refresh directory |
| `?` | Help popup |
| `q` | Quit |

## Adding custom presets

Edit or create any `.preset` file in `~/.config/nfm/presets/`:

```ini
[My Custom Preset]
type=video
description=Re-encode to AV1 at CRF 28
args=-c:v libsvtav1 -crf 28 -preset 6 -c:a libopus -b:a 128k
ext=mkv
```

Restart nfm to pick up the changes.

## Requirements

- ffmpeg ≥ 4.0 (tested with 6.1.1)
- libncurses ≥ 6
- gcc or clang, make

## License

MIT

---

## Changelog

### [1.0.3] — 2026-04-14

#### Added
- **Rename files and folders** — press `F2` or `e`; full editing with cursor movement, Backspace, Delete, Home/End; UTF-8-aware; ESC cancels

#### Fixed
- **Garbled Unicode / emoji in filenames on Linux** — now links against `libncursesw` for proper wide-character rendering
- Filename truncation in Actions popup now UTF-8-safe (no more split byte sequences)

### [1.0.2] — 2026-04-14

#### Added
- **Filter-as-you-type in file browser** — press `/` to open the filter bar and
  start typing; the listing narrows in real-time to entries whose names contain
  the typed substring (case-insensitive, UTF-8-aware: typing `ün` finds
  `München`, typing `3` finds `123` and `367`)
- Filter bar shows match count while active (`/foo  (3 matches)`)
- `ESC` while typing closes the bar but keeps the filter; `ESC` again (or
  backspace on an empty bar) clears the filter entirely
- Arrow keys work normally during filtering so you can navigate while searching

### [1.0.1] — 2026-04-14

#### Fixed
- **Garbled characters on macOS and Linux** — `setlocale(LC_ALL, "")` is now
  called before `initscr()` so ncurses uses UTF-8; this fixes garbage output
  for any multi-byte terminal characters
- **Umlaut / special-character filenames on macOS** — same locale fix; NFD-
  composed filenames (ä ö ü etc.) now display correctly in the browser
- Replaced raw Unicode symbols in footer/menu strings with ASCII equivalents
  as additional fallback for non-UTF-8 terminals

#### Added
- **Media-only browser filter** (`m` key) — hides non-processable files
  (`.docx`, `.txt`, executables …) so only video, audio and image files plus
  directories are shown; `[media]` indicator appears in the browser title bar
  when the filter is active

### [1.0.0] — 2026-04-14

#### Added
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
