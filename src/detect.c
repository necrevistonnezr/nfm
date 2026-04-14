/*
 * detect.c — Locate ffmpeg/ffprobe binaries and probe hardware capabilities
 */
#define _POSIX_C_SOURCE 200809L

#include "nfm.h"
#include "detect.h"

/* Candidate binary names / paths (searched in order) */
static const char *FFMPEG_CANDS[] = {
    "ffmpeg",
    "/usr/bin/ffmpeg",
    "/usr/local/bin/ffmpeg",
    "/opt/homebrew/bin/ffmpeg",
    "/opt/homebrew/opt/ffmpeg/bin/ffmpeg",
    NULL
};
static const char *FFPROBE_CANDS[] = {
    "ffprobe",
    "/usr/bin/ffprobe",
    "/usr/local/bin/ffprobe",
    "/opt/homebrew/bin/ffprobe",
    "/opt/homebrew/opt/ffmpeg/bin/ffprobe",
    NULL
};

/* Return 1 if the absolute path points to an executable file. */
static int is_exec(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && (st.st_mode & S_IXUSR));
}

/* Find the first working candidate; write result into buf/buflen. */
static int find_binary(const char **cands, char *buf, size_t buflen) {
    for (int i = 0; cands[i]; i++) {
        const char *c = cands[i];
        if (c[0] == '/') {
            if (is_exec(c)) {
                snprintf(buf, buflen, "%s", c);
                return 1;
            }
        } else {
            /* Search PATH */
            const char *path_env = getenv("PATH");
            if (!path_env) continue;
            char *env_copy = strdup(path_env);
            if (!env_copy) continue;
            char *dir = strtok(env_copy, ":");
            while (dir) {
                char full[PATH_MAX];
                snprintf(full, sizeof(full), "%s/%s", dir, c);
                if (is_exec(full)) {
                    snprintf(buf, buflen, "%s", full);
                    free(env_copy);
                    return 1;
                }
                dir = strtok(NULL, ":");
            }
            free(env_copy);
        }
    }
    return 0;
}

int detect_ffmpeg(Capabilities *caps) {
    memset(caps, 0, sizeof(*caps));

    caps->ffmpeg_found  = find_binary(FFMPEG_CANDS,  caps->ffmpeg_path,  sizeof(caps->ffmpeg_path));
    caps->ffprobe_found = find_binary(FFPROBE_CANDS, caps->ffprobe_path, sizeof(caps->ffprobe_path));

    if (caps->ffmpeg_found) {
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof(cmd), "%s -version 2>&1", caps->ffmpeg_path);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            char line[256];
            if (fgets(line, sizeof(line), fp)) {
                char *ver = strstr(line, "version ");
                if (ver) {
                    ver += 8;
                    char *sp = ver;
                    while (*sp && *sp != ' ' && *sp != '\n') sp++;
                    *sp = '\0';
                    snprintf(caps->ffmpeg_version, sizeof(caps->ffmpeg_version), "%s", ver);
                }
            }
            pclose(fp);
        }
    }

    return caps->ffmpeg_found;
}

int detect_capabilities(Capabilities *caps) {
    if (!caps->ffmpeg_found) return 0;

    char cmd[PATH_MAX + 64];
    char line[512];

    /* Hardware accelerators */
    snprintf(cmd, sizeof(cmd), "%s -hwaccels -hide_banner 2>/dev/null", caps->ffmpeg_path);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "nvenc") || strstr(line, "cuda"))   caps->has_nvenc = 1;
            if (strstr(line, "vaapi"))                           caps->has_vaapi = 1;
            if (strstr(line, "videotoolbox"))                    caps->has_videotoolbox = 1;
            if (strstr(line, "qsv"))                             caps->has_qsv = 1;
            if (strstr(line, "cuda"))                            caps->has_cuda = 1;
        }
        pclose(fp);
    }

    /* Encoders — look for specific codec names */
    snprintf(cmd, sizeof(cmd), "%s -encoders -hide_banner 2>/dev/null", caps->ffmpeg_path);
    fp = popen(cmd, "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "libx264"))                              caps->has_libx264 = 1;
            if (strstr(line, "libx265"))                              caps->has_libx265 = 1;
            if (strstr(line, "libvpx-vp9") || strstr(line, "libvpx_vp9")) caps->has_libvpx_vp9 = 1;
            if (strstr(line, "libmp3lame"))                           caps->has_libmp3lame = 1;
            if (strstr(line, "libopus"))                              caps->has_libopus = 1;
            if (strstr(line, "libsvtav1"))                            caps->has_libsvtav1 = 1;
            if (strstr(line, "libfdk_aac"))                           caps->has_libfdk_aac = 1;
            /* match standalone "aac" encoder but not "libfdk_aac" etc. */
            {
                char *p = strstr(line, " aac ");
                if (p) caps->has_aac = 1;
            }
        }
        pclose(fp);
    }
    /* built-in AAC is present in virtually every ffmpeg build */
    caps->has_aac = 1;

    return 1;
}

int offer_install_ffmpeg(void) {
    /* Called after endwin(); restores initscr() at the end. */
    printf("\n");
#if defined(__APPLE__)
    printf("Trying to install ffmpeg via Homebrew...\n");
    printf("  brew install ffmpeg\n\n");
    int r = system("brew install ffmpeg");
#elif defined(__linux__)
    printf("Trying to install ffmpeg via apt...\n");
    printf("  sudo apt-get install -y ffmpeg\n\n");
    int r = system("sudo apt-get install -y ffmpeg");
#else
    printf("Automatic installation is not supported on this platform.\n");
    printf("Please install ffmpeg manually and re-run nfm.\n");
    int r = 1;
#endif
    printf("\nPress Enter to continue...");
    fflush(stdout);
    getchar();
    return (r == 0);
}
