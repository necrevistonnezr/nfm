/*
 * nfm.h — Shared types, constants and function declarations
 * nfm — Flexible ncurses video converter
 */
#ifndef NFM_H
#define NFM_H

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE   700

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <strings.h>
#include <locale.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>

/* ─── Version ─────────────────────────────────────────────────────────── */
#define NFM_VERSION  "1.0.0"
#define NFM_NAME     "nfm"

/* ─── Limits ──────────────────────────────────────────────────────────── */
#define NFM_MAX_PRESETS   256
#define NFM_MAX_FILES    4096
#define NFM_MAX_ARGS      128   /* max argv entries for ffmpeg */
#define NFM_ARGS_BUFSIZE 2048   /* buffer for split arg strings */

/* ─── Color pairs ─────────────────────────────────────────────────────── */
#define CP_HEADER    1   /* white on blue  — header / footer bars  */
#define CP_FOOTER    2   /* white on blue  — footer                 */
#define CP_SELECTED  3   /* black on cyan  — highlighted row        */
#define CP_DIR       4   /* bold cyan      — directory entries      */
#define CP_VIDEO     5   /* cyan           — video presets          */
#define CP_AUDIO     6   /* yellow         — audio presets          */
#define CP_SPECIAL   7   /* green          — special presets        */
#define CP_ERROR     8   /* bold red       — error messages         */
#define CP_PROGRESS  9   /* green          — progress bar fill      */
#define CP_TITLE    10   /* bold white     — section headings       */
#define CP_DIM      11   /* dim white      — secondary labels       */
#define CP_CUSTOM   12   /* magenta        — custom-settings fields */
#define CP_INFO     13   /* cyan           — info-panel values      */
#define CP_BORDER   14   /* white          — popup borders          */

/* ─── Application states ──────────────────────────────────────────────── */
typedef enum {
    STATE_BROWSER,
    STATE_FILE_MENU,
    STATE_PRESET_MENU,
    STATE_CUSTOM,
    STATE_PROGRESS,     /* handled inside run_encoding() */
    STATE_RESULT,
    STATE_QUIT
} AppState;

/* ─── File types ──────────────────────────────────────────────────────── */
typedef enum {
    FILE_TYPE_OTHER = 0,
    FILE_TYPE_VIDEO,
    FILE_TYPE_AUDIO,
    FILE_TYPE_IMAGE,
    FILE_TYPE_DIR
} FileType;

/* ─── Directory entry ─────────────────────────────────────────────────── */
typedef struct {
    char      name[NAME_MAX + 1];
    char      fullpath[PATH_MAX];
    FileType  type;
    long long size;
    int       is_dir;
    int       is_hidden;
} FileEntry;

/* ─── ffprobe result ──────────────────────────────────────────────────── */
typedef struct {
    int   valid;
    char  format_name[64];
    char  format_long_name[256];
    double duration;          /* seconds      */
    long long size;           /* bytes        */
    long long bitrate;        /* bps overall  */

    /* video stream */
    int       has_video;
    char      v_codec[64];
    char      v_codec_long[128];
    int       v_width;
    int       v_height;
    char      v_fps_str[16];  /* e.g. "24/1" */
    float     v_fps;
    long long v_bitrate;

    /* audio stream */
    int       has_audio;
    char      a_codec[64];
    int       a_sample_rate;
    int       a_channels;
    long long a_bitrate;
} ProbeResult;

/* ─── Preset (loaded from .preset file) ───────────────────────────────── */
typedef struct {
    char name[128];
    char type[16];          /* "video" | "audio" | "special" */
    char description[256];
    char args[1024];        /* ffmpeg arguments string       */
    char ext[16];           /* output file extension         */
    int  estimate_savings;  /* show savings estimate first   */
    int  color_pair;        /* ncurses color pair            */
    char input_ext[16];     /* restrict to this input ext    */
} Preset;

/* ─── Binary + hardware capabilities ─────────────────────────────────── */
typedef struct {
    int  ffmpeg_found;
    int  ffprobe_found;
    char ffmpeg_path[PATH_MAX];
    char ffprobe_path[PATH_MAX];
    char ffmpeg_version[64];

    /* hardware encoders */
    int has_nvenc;
    int has_vaapi;
    int has_videotoolbox;
    int has_qsv;
    int has_cuda;

    /* software codecs */
    int has_libx264;
    int has_libx265;
    int has_libvpx_vp9;
    int has_libmp3lame;
    int has_libopus;
    int has_aac;
    int has_libsvtav1;
    int has_libfdk_aac;
} Capabilities;

/* ─── Custom-encoding form state ──────────────────────────────────────── */
typedef struct {
    int cursor;          /* active field index (0-based) */
    int v_codec_idx;     /* 0=libx264 1=libx265 2=copy 3=none */
    int a_codec_idx;     /* 0=aac 1=libmp3lame 2=libopus 3=copy 4=none */
    int crf;             /* 0-51 */
    int speed_idx;       /* 0=medium 1=slow 2=fast 3=veryfast 4=veryslow */
    int res_idx;         /* 0=Original 1=4K 2=1080p 3=720p 4=480p */
    int container_idx;   /* 0=auto 1=mp4 2=mkv 3=webm 4=m4a 5=mp3 */
    int a_bitrate_idx;   /* 0=128k 1=192k 2=256k 3=320k */
} CustomSettings;

/* ─── Live progress data ──────────────────────────────────────────────── */
typedef struct {
    double    pct;              /* 0.0 – 100.0  */
    double    fps;
    double    speed;            /* e.g. 2.3x    */
    long long size_bytes;
    char      time_str[16];
    char      eta_str[16];
    long long bitrate_kbps;
    long long frame;
    int       done;
    int       error;
    char      log[6][256];
    int       log_count;
} ProgressData;

/* ─── Application context ─────────────────────────────────────────────── */
typedef struct {
    AppState state;

    char current_path[PATH_MAX];

    /* file browser */
    FileEntry *files;
    int        file_count;
    int        browser_cursor;
    int        browser_scroll;
    int        show_hidden;
    int        show_media_only;  /* 1 = hide non-media files in browser */

    /* selected file */
    char      selected_file[PATH_MAX];
    FileType  selected_type;
    ProbeResult probe;
    int         probe_loaded;   /* 1 once probe ran for selected file */

    /* file-action menu */
    int menu_cursor;

    /* preset menu */
    Preset *presets;
    int     preset_count;
    int     preset_cursor;
    int     preset_scroll;

    Capabilities caps;
    CustomSettings custom;
    ProgressData   progress;

    /* result */
    char      output_file[PATH_MAX];
    long long input_size_result;
    long long output_size_result;
    double    encoding_elapsed;  /* seconds */

    /* ncurses windows */
    WINDOW *win_header;
    WINDOW *win_browser;
    WINDOW *win_info;
    WINDOW *win_footer;

    int rows, cols;

    char status_msg[256];
    int  status_is_error;
} AppCtx;

/* ─── detect.c ────────────────────────────────────────────────────────── */
int detect_ffmpeg(Capabilities *caps);
int detect_capabilities(Capabilities *caps);
int offer_install_ffmpeg(void);

/* ─── probe.c ─────────────────────────────────────────────────────────── */
int   probe_file(const char *path, const Capabilities *caps, ProbeResult *result);
void  format_duration(double seconds, char *buf, int buflen);
void  format_size(long long bytes, char *buf, int buflen);
void  format_bitrate(long long bps, char *buf, int buflen);
float parse_fps_str(const char *fps_str);

/* ─── presets.c ───────────────────────────────────────────────────────── */
int   load_presets(Preset **out, int *count_out, const Capabilities *caps);
void  free_presets(Preset *presets);
int   init_user_preset_dir(void);
float estimate_savings(const ProbeResult *probe, const char *target_args);

/* ─── progress.c ──────────────────────────────────────────────────────── */
int run_encoding(AppCtx *ctx, const char *input, const char *extra_args,
                 const char *output, double total_duration);

#endif /* NFM_H */
