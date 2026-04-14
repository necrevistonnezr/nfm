/*
 * probe.c — Run ffprobe and parse its output into a ProbeResult struct
 */
#define _POSIX_C_SOURCE 200809L

#include "nfm.h"
#include "probe.h"

/* ── Utility formatters ────────────────────────────────────────────────── */

float parse_fps_str(const char *s) {
    if (!s || !*s) return 0.0f;
    int num = 0, den = 1;
    sscanf(s, "%d/%d", &num, &den);
    if (den == 0) return 0.0f;
    return (float)num / (float)den;
}

void format_duration(double sec, char *buf, int n) {
    if (sec <= 0) { snprintf(buf, n, "N/A"); return; }
    int h = (int)(sec / 3600);
    int m = (int)((sec - h * 3600) / 60);
    int s = (int)(sec - h * 3600 - m * 60);
    if (h > 0)
        snprintf(buf, n, "%d:%02d:%02d", h, m, s);
    else
        snprintf(buf, n, "%d:%02d", m, s);
}

void format_size(long long bytes, char *buf, int n) {
    if (bytes <= 0) { snprintf(buf, n, "N/A"); return; }
    if      (bytes >= (long long)1 << 30)
        snprintf(buf, n, "%.2f GB", (double)bytes / (1 << 30));
    else if (bytes >= 1 << 20)
        snprintf(buf, n, "%.1f MB", (double)bytes / (1 << 20));
    else if (bytes >= 1 << 10)
        snprintf(buf, n, "%.1f KB", (double)bytes / (1 << 10));
    else
        snprintf(buf, n, "%lld B", bytes);
}

void format_bitrate(long long bps, char *buf, int n) {
    if (bps <= 0) { snprintf(buf, n, "N/A"); return; }
    if      (bps >= 1000000)
        snprintf(buf, n, "%.1f Mbps", (double)bps / 1e6);
    else if (bps >= 1000)
        snprintf(buf, n, "%.0f kbps", (double)bps / 1e3);
    else
        snprintf(buf, n, "%lld bps", bps);
}

/* ── Main probe function ───────────────────────────────────────────────── */

int probe_file(const char *path, const Capabilities *caps, ProbeResult *r) {
    memset(r, 0, sizeof(*r));
    if (!caps->ffprobe_found) return 0;

    int pipefd[2];
    if (pipe(pipefd) < 0) return 0;

    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return 0; }

    if (pid == 0) {
        /* child */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        /* suppress ffprobe's own messages */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);

        char *argv[] = {
            (char *)caps->ffprobe_path,
            "-v", "quiet",
            "-show_entries",
            "format=duration,size,bit_rate,format_name,format_long_name:"
            "stream=codec_name,codec_long_name,codec_type,"
            "width,height,bit_rate,sample_rate,channels,r_frame_rate",
            "-of", "default=noprint_wrappers=1",
            (char *)path,
            NULL
        };
        execv(argv[0], argv);
        _exit(1);
    }

    /* parent — read pipe */
    close(pipefd[1]);
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) { close(pipefd[0]); waitpid(pid, NULL, 0); return 0; }

    char line[512];
    char stream_type[16] = "";   /* tracks current stream context */
    int  in_video_stream  = 0;
    int  in_audio_stream  = 0;

    while (fgets(line, sizeof(line), fp)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        /* ── stream type marker ── */
        if (strcmp(key, "codec_type") == 0) {
            snprintf(stream_type, sizeof(stream_type), "%s", val);
            in_video_stream = (strcmp(val, "video") == 0);
            in_audio_stream = (strcmp(val, "audio") == 0);
            continue;
        }

        /* ── format fields (no stream_type set yet at this point) ── */
        if (strcmp(key, "format_name") == 0) {
            snprintf(r->format_name, sizeof(r->format_name), "%s", val);
        } else if (strcmp(key, "format_long_name") == 0) {
            snprintf(r->format_long_name, sizeof(r->format_long_name), "%s", val);
        } else if (strcmp(key, "duration") == 0) {
            double d = atof(val);
            if (d > 0 && r->duration == 0) r->duration = d;
        } else if (strcmp(key, "size") == 0) {
            long long sz = atoll(val);
            if (sz > 0 && r->size == 0) r->size = sz;
        } else if (strcmp(key, "bit_rate") == 0) {
            long long br = atoll(val);
            if (br > 0) {
                if      (in_video_stream && !r->v_bitrate) r->v_bitrate = br;
                else if (in_audio_stream && !r->a_bitrate) r->a_bitrate = br;
                else if (!in_video_stream && !in_audio_stream && !r->bitrate)
                    r->bitrate = br;
            }

        /* ── stream fields ── */
        } else if (strcmp(key, "codec_name") == 0) {
            if (in_video_stream && !r->has_video) {
                snprintf(r->v_codec, sizeof(r->v_codec), "%s", val);
                r->has_video = 1;
            } else if (in_audio_stream && !r->has_audio) {
                snprintf(r->a_codec, sizeof(r->a_codec), "%s", val);
                r->has_audio = 1;
            }
        } else if (strcmp(key, "codec_long_name") == 0) {
            if (in_video_stream && !r->v_codec_long[0])
                snprintf(r->v_codec_long, sizeof(r->v_codec_long), "%s", val);
        } else if (strcmp(key, "width") == 0) {
            if (in_video_stream && r->v_width == 0) r->v_width = atoi(val);
        } else if (strcmp(key, "height") == 0) {
            if (in_video_stream && r->v_height == 0) r->v_height = atoi(val);
        } else if (strcmp(key, "r_frame_rate") == 0) {
            if (in_video_stream && !r->v_fps_str[0]) {
                snprintf(r->v_fps_str, sizeof(r->v_fps_str), "%s", val);
                r->v_fps = parse_fps_str(val);
            }
        } else if (strcmp(key, "sample_rate") == 0) {
            if (in_audio_stream && !r->a_sample_rate) r->a_sample_rate = atoi(val);
        } else if (strcmp(key, "channels") == 0) {
            if (in_audio_stream && !r->a_channels) r->a_channels = atoi(val);
        }
    }

    fclose(fp);
    waitpid(pid, NULL, 0);

    r->valid = (r->size > 0 || r->duration > 0 || r->has_video || r->has_audio);
    return r->valid;
}
