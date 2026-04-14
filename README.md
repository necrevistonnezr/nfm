# nfm — Flexible ncurses Video Converter

A keyboard-driven terminal frontend for **ffmpeg**, built with ncurses.  
Inspired by [nano-ffmpeg](https://github.com/dgr8akki/nano-ffmpeg), rewritten in C.

```
┌─ nfm 1.0.0 ─────────────────────────────── /home/user/Videos ─┐
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
sudo dpkg -i build/nfm_1.0.0_amd64.deb
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
| `.` | Toggle hidden files |
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
