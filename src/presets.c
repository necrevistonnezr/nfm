/*
 * presets.c — Load presets from .preset text files
 *
 * Search order:
 *   1. ~/.config/nfm/presets/
 *   2. /usr/share/nfm/presets/
 *   3. /usr/local/share/nfm/presets/
 *
 * The first directory that contains at least one .preset file wins.
 * On first run the user preset directory is created (and system presets
 * are copied there if available), so users can customise freely.
 */
#define _POSIX_C_SOURCE 200809L

#include "nfm.h"
#include "presets.h"

/* ── Helper: user preset directory ────────────────────────────────────── */

void get_user_preset_dir(char *buf, size_t n) {
    const char *home = getenv("HOME");
    if (home) snprintf(buf, n, "%s/.config/nfm/presets", home);
    else       snprintf(buf, n, "/tmp/.nfm_presets");
}

int init_user_preset_dir(void) {
    const char *home = getenv("HOME");
    if (!home) return 0;

    char cfg[PATH_MAX], dir[PATH_MAX];
    snprintf(cfg, sizeof(cfg), "%s/.config/nfm", home);
    snprintf(dir, sizeof(dir), "%s/presets", cfg);

    mkdir(cfg, 0755);
    mkdir(dir, 0755);

    /* Check whether presets already live there */
    char test[PATH_MAX];
    snprintf(test, sizeof(test), "%s/video.preset", dir);
    struct stat st;
    if (stat(test, &st) == 0) return 1;   /* already initialised */

    /* Try to copy from system directories */
    static const char *sys_dirs[] = {
        "/usr/share/nfm/presets",
        "/usr/local/share/nfm/presets",
        NULL
    };
    for (int i = 0; sys_dirs[i]; i++) {
        char src[PATH_MAX];
        snprintf(src, sizeof(src), "%s/video.preset", sys_dirs[i]);
        if (stat(src, &st) == 0) {
            char cmd[PATH_MAX * 3];
            /* -n: don't overwrite existing files */
            snprintf(cmd, sizeof(cmd), "cp -n '%s'/*.preset '%s/' 2>/dev/null",
                     sys_dirs[i], dir);
            system(cmd);
            return 1;
        }
    }
    return 0;
}

/* ── INI-style .preset file parser ────────────────────────────────────── */

static int parse_preset_file(const char *filepath, Preset *buf, int *count, int maxn) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;

    char line[1024];
    Preset *cur = NULL;
    int added = 0;

    while (fgets(line, sizeof(line), fp)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';

        /* skip blanks and comments */
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#' || *p == ';') continue;

        /* section header  [Preset Name] */
        if (*p == '[') {
            const char *end = strchr(p + 1, ']');
            if (!end) continue;
            if (*count >= maxn) break;
            cur = &buf[*count];
            memset(cur, 0, sizeof(*cur));
            int len = (int)(end - (p + 1));
            if (len >= (int)sizeof(cur->name)) len = (int)sizeof(cur->name) - 1;
            memcpy(cur->name, p + 1, len);
            cur->name[len] = '\0';
            (*count)++;
            added++;
            continue;
        }

        if (!cur) continue;

        /* key = value */
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        while (*key == ' ' || *key == '\t') key++;
        while (*val == ' ' || *val == '\t') val++;
        /* trim trailing spaces from key */
        char *ek = key + strlen(key) - 1;
        while (ek > key && (*ek == ' ' || *ek == '\t')) { *ek = '\0'; ek--; }

        if      (strcmp(key, "type") == 0)
            snprintf(cur->type, sizeof(cur->type), "%s", val);
        else if (strcmp(key, "description") == 0)
            snprintf(cur->description, sizeof(cur->description), "%s", val);
        else if (strcmp(key, "args") == 0)
            snprintf(cur->args, sizeof(cur->args), "%s", val);
        else if (strcmp(key, "ext") == 0)
            snprintf(cur->ext, sizeof(cur->ext), "%s", val);
        else if (strcmp(key, "estimate_savings") == 0)
            cur->estimate_savings = atoi(val);
        else if (strcmp(key, "input_ext") == 0)
            snprintf(cur->input_ext, sizeof(cur->input_ext), "%s", val);
    }

    fclose(fp);
    return added;
}

static void assign_colors(Preset *p, int n) {
    for (int i = 0; i < n; i++) {
        if      (strcmp(p[i].type, "video")   == 0) p[i].color_pair = CP_VIDEO;
        else if (strcmp(p[i].type, "audio")   == 0) p[i].color_pair = CP_AUDIO;
        else if (strcmp(p[i].type, "special") == 0) p[i].color_pair = CP_SPECIAL;
        else                                         p[i].color_pair = CP_TITLE;
    }
}

/* ── Public API ────────────────────────────────────────────────────────── */

int load_presets(Preset **out, int *count_out, const Capabilities *caps) {
    (void)caps;

    Preset *buf = calloc(NFM_MAX_PRESETS, sizeof(Preset));
    if (!buf) return 0;

    int count = 0;

    char user_dir[PATH_MAX];
    get_user_preset_dir(user_dir, sizeof(user_dir));

    const char *search[] = {
        user_dir,
        "/usr/share/nfm/presets",
        "/usr/local/share/nfm/presets",
        NULL
    };

    for (int d = 0; search[d] && count < NFM_MAX_PRESETS; d++) {
        DIR *dir = opendir(search[d]);
        if (!dir) continue;

        struct dirent *ent;
        int found_in_dir = 0;
        while ((ent = readdir(dir)) != NULL && count < NFM_MAX_PRESETS) {
            const char *nm = ent->d_name;
            size_t len = strlen(nm);
            if (len < 8) continue;
            if (strcmp(nm + len - 7, ".preset") != 0) continue;

            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", search[d], nm);
            int n = parse_preset_file(path, buf + count, &count, NFM_MAX_PRESETS);
            if (n > 0) found_in_dir = 1;
        }
        closedir(dir);

        /* Stop after first directory that delivered presets */
        if (found_in_dir) break;
    }

    assign_colors(buf, count);

    *out       = buf;
    *count_out = count;
    return count;
}

void free_presets(Preset *p) {
    free(p);
}

float estimate_savings(const ProbeResult *probe, const char *target_args) {
    if (!probe || !probe->has_video) return 0.0f;

    int to_x265 = (strstr(target_args, "libx265") || strstr(target_args, "hevc")) ? 1 : 0;
    int to_av1  = (strstr(target_args, "av1")     || strstr(target_args, "svtav1")) ? 1 : 0;

    const char *vc = probe->v_codec;

    if (strcmp(vc, "h264") == 0 || strcmp(vc, "avc") == 0 || strcmp(vc, "libx264") == 0) {
        if (to_av1)  return 0.55f;
        if (to_x265) return 0.45f;
    }
    if (strcmp(vc, "hevc") == 0 || strcmp(vc, "h265") == 0 || strcmp(vc, "libx265") == 0) {
        if (to_av1)  return 0.30f;
        return 0.15f;
    }
    if (strcmp(vc, "mpeg4") == 0 || strcmp(vc, "xvid") == 0 || strcmp(vc, "divx") == 0
        || strcmp(vc, "msmpeg4v3") == 0) {
        if (to_x265) return 0.60f;
        return 0.50f;
    }
    if (strcmp(vc, "vp9") == 0 || strcmp(vc, "libvpx-vp9") == 0) {
        if (to_av1) return 0.30f;
    }

    return 0.40f;
}
