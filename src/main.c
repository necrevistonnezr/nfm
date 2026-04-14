/*
 * main.c — nfm: flexible ncurses video converter
 *
 * Screens / states
 *   STATE_BROWSER      file browser  (left) + probe info (right)
 *   STATE_FILE_MENU    popup: Presets / Custom / Capabilities / Back
 *   STATE_PRESET_MENU  colour-coded preset list
 *   STATE_CUSTOM       custom-encoding form
 *   STATE_PROGRESS     handled inside progress.c
 *   STATE_RESULT       encoding-complete summary
 */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE   700

#include "nfm.h"
#include "detect.h"
#include "probe.h"
#include "presets.h"
#include "progress.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Identify media type from file extension (lowercase) */
static FileType ext_to_type(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return FILE_TYPE_OTHER;
    char e[16] = {0};
    for (int i = 0; dot[i+1] && i < 14; i++) e[i] = (char)tolower((unsigned char)dot[i+1]);

    static const char *vid[] = {"mp4","mkv","avi","mov","wmv","flv","webm","m4v",
        "mpg","mpeg","3gp","ogv","ts","m2ts","vob","rmvb","f4v","h264","h265","hevc",
        "asf","divx","xvid",NULL};
    static const char *aud[] = {"mp3","aac","flac","wav","ogg","wma","m4a","opus",
        "ac3","dts","aiff","ape","mka","mpa",NULL};
    static const char *img[] = {"gif","png","jpg","jpeg","bmp","webp","tiff",NULL};

    for (int i = 0; vid[i]; i++) if (strcmp(e, vid[i]) == 0) return FILE_TYPE_VIDEO;
    for (int i = 0; aud[i]; i++) if (strcmp(e, aud[i]) == 0) return FILE_TYPE_AUDIO;
    for (int i = 0; img[i]; i++) if (strcmp(e, img[i]) == 0) return FILE_TYPE_IMAGE;
    return FILE_TYPE_OTHER;
}

static int cmp_files(const void *a, const void *b) {
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;
    if (strcmp(fa->name, "..") == 0) return -1;
    if (strcmp(fb->name, "..") == 0) return  1;
    if (fa->is_dir && !fb->is_dir) return -1;
    if (!fa->is_dir && fb->is_dir) return  1;
    return strcasecmp(fa->name, fb->name);
}

/* Macro: access the i-th currently visible file */
#define BFILE(ctx, i) ((ctx)->all_files[(ctx)->show_idx[(i)]])

/* Case-insensitive substring search (byte-level; fine for UTF-8 when the
 * needle is typed in the same encoding as the haystack). */
static int contains_icase(const char *hay, const char *needle) {
    if (!needle || !*needle) return 1;
    size_t nl = strlen(needle);
    for (; *hay; hay++)
        if (strncasecmp(hay, needle, nl) == 0) return 1;
    return 0;
}

/* Remove the last UTF-8 codepoint from buf (handles multi-byte chars). */
static void filter_backspace(char *buf, int *len) {
    if (*len == 0) return;
    int i = *len - 1;
    /* step back over continuation bytes (10xxxxxx) */
    while (i > 0 && ((unsigned char)buf[i] & 0xC0) == 0x80) i--;
    buf[i] = '\0';
    *len   = i;
}

/* Rebuild show_idx[] from all_files[] using the current filter string. */
static void apply_filter(AppCtx *ctx) {
    int n = 0;
    for (int i = 0; i < ctx->all_count && n < NFM_MAX_FILES; i++) {
        FileEntry *fe = &ctx->all_files[i];
        if (strcmp(fe->name, "..") == 0) {      /* ".." is always visible */
            ctx->show_idx[n++] = i;
            continue;
        }
        if (ctx->filter_len == 0 || contains_icase(fe->name, ctx->filter_buf))
            ctx->show_idx[n++] = i;
    }
    ctx->file_count = n;
    if (ctx->browser_cursor >= ctx->file_count)
        ctx->browser_cursor = ctx->file_count > 0 ? ctx->file_count - 1 : 0;
    if (ctx->browser_scroll > ctx->browser_cursor)
        ctx->browser_scroll = ctx->browser_cursor;
    ctx->probe_loaded = 0;
}

static void load_directory(AppCtx *ctx) {
    free(ctx->all_files);
    ctx->all_files     = NULL;
    ctx->all_count     = 0;
    ctx->file_count    = 0;
    ctx->browser_cursor= 0;
    ctx->browser_scroll= 0;

    DIR *d = opendir(ctx->current_path);
    if (!d) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 "Cannot open: %s", ctx->current_path);
        ctx->status_is_error = 1;
        return;
    }

    ctx->all_files = malloc(NFM_MAX_FILES * sizeof(FileEntry));
    if (!ctx->all_files) { closedir(d); return; }

    struct dirent *ent;
    while ((ent = readdir(d)) && ctx->all_count < NFM_MAX_FILES) {
        const char *nm = ent->d_name;
        if (strcmp(nm, ".") == 0) continue;
        if (nm[0] == '.' && strcmp(nm, "..") != 0 && !ctx->show_hidden) continue;
        if (ctx->show_media_only && strcmp(nm, "..") != 0) {
            FileType t = ext_to_type(nm);
            struct stat _st;
            char _fp[PATH_MAX];
            snprintf(_fp, sizeof(_fp), "%s/%s", ctx->current_path, nm);
            if (stat(_fp, &_st) == 0 && !S_ISDIR(_st.st_mode)
                && t == FILE_TYPE_OTHER) continue;
        }

        FileEntry *fe = &ctx->all_files[ctx->all_count];
        memset(fe, 0, sizeof(*fe));
        snprintf(fe->name,     sizeof(fe->name),     "%s", nm);
        snprintf(fe->fullpath, sizeof(fe->fullpath),  "%s/%s", ctx->current_path, nm);
        fe->is_hidden = (nm[0] == '.' && strcmp(nm, "..") != 0);

        struct stat st;
        if (stat(fe->fullpath, &st) == 0) {
            fe->is_dir = S_ISDIR(st.st_mode);
            fe->size   = st.st_size;
        }
        fe->type = fe->is_dir ? FILE_TYPE_DIR : ext_to_type(nm);
        ctx->all_count++;
    }
    closedir(d);
    qsort(ctx->all_files, ctx->all_count, sizeof(FileEntry), cmp_files);
    apply_filter(ctx);
}

/* Build output filename: <dir>/<base>_<suffix>.<ext> */
static void make_output_path(const char *input, const char *suffix,
                              const char *ext, char *out, size_t n) {
    char dir[PATH_MAX], base[NAME_MAX];
    snprintf(dir,  sizeof(dir),  "%s", input);
    char *sl = strrchr(dir, '/');
    if (sl) { *sl = '\0'; }
    else    { dir[0] = '.'; dir[1] = '\0'; }

    const char *bn = strrchr(input, '/');
    bn = bn ? bn + 1 : input;
    snprintf(base, sizeof(base), "%s", bn);
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';

    snprintf(out, n, "%s/%s_%s.%s", dir, base, suffix, ext);
    /* avoid collision with input */
    if (strcmp(out, input) == 0)
        snprintf(out, n, "%s/%s_%s_out.%s", dir, base, suffix, ext);
}

/* ── ncurses init / cleanup ─────────────────────────────────────────────── */

static volatile int g_resize = 0;
#ifdef SIGWINCH
static void sigwinch_handler(int s) { (void)s; g_resize = 1; }
#endif

static void init_colors(void) {
    if (!has_colors()) return;
    start_color();
    use_default_colors();
    init_pair(CP_HEADER,   COLOR_WHITE,   COLOR_BLUE);
    init_pair(CP_FOOTER,   COLOR_WHITE,   COLOR_BLUE);
    init_pair(CP_SELECTED, COLOR_BLACK,   COLOR_CYAN);
    init_pair(CP_DIR,      COLOR_CYAN,    -1);
    init_pair(CP_VIDEO,    COLOR_CYAN,    -1);
    init_pair(CP_AUDIO,    COLOR_YELLOW,  -1);
    init_pair(CP_SPECIAL,  COLOR_GREEN,   -1);
    init_pair(CP_ERROR,    COLOR_RED,     -1);
    init_pair(CP_PROGRESS, COLOR_GREEN,   -1);
    init_pair(CP_TITLE,    COLOR_WHITE,   -1);
    init_pair(CP_DIM,      COLOR_WHITE,   -1);
    init_pair(CP_CUSTOM,   COLOR_MAGENTA, -1);
    init_pair(CP_INFO,     COLOR_CYAN,    -1);
    init_pair(CP_BORDER,   COLOR_WHITE,   -1);
}

static void create_windows(AppCtx *ctx) {
    getmaxyx(stdscr, ctx->rows, ctx->cols);
    int bw  = (ctx->cols * 3) / 5;
    int iw  = ctx->cols - bw;
    int ch  = ctx->rows - 2;   /* content height */

    ctx->win_header  = newwin(1,  ctx->cols, 0,    0);
    ctx->win_browser = newwin(ch, bw,        1,    0);
    ctx->win_info    = newwin(ch, iw,        1,    bw);
    ctx->win_footer  = newwin(1,  ctx->cols, ctx->rows - 1, 0);

    keypad(ctx->win_browser, TRUE);
    wtimeout(ctx->win_browser, 50);
}

static void destroy_windows(AppCtx *ctx) {
    if (ctx->win_header)  { delwin(ctx->win_header);  ctx->win_header  = NULL; }
    if (ctx->win_browser) { delwin(ctx->win_browser); ctx->win_browser = NULL; }
    if (ctx->win_info)    { delwin(ctx->win_info);    ctx->win_info    = NULL; }
    if (ctx->win_footer)  { delwin(ctx->win_footer);  ctx->win_footer  = NULL; }
}

static void handle_resize(AppCtx *ctx) {
    g_resize = 0;
    endwin();
    refresh();
    destroy_windows(ctx);
    create_windows(ctx);
    clearok(stdscr, TRUE);
    refresh();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Drawing functions
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_header(AppCtx *ctx) {
    WINDOW *w = ctx->win_header;
    werase(w);
    wattron(w, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwhline(w, 0, 0, ' ', ctx->cols);
    mvwprintw(w, 0, 1, " nfm %s", NFM_VERSION);
    /* show current path, truncated if needed */
    int pathlen = (int)strlen(ctx->current_path);
    int avail   = ctx->cols - 14;
    if (avail > 0) {
        const char *p = ctx->current_path;
        if (pathlen > avail) p = ctx->current_path + (pathlen - avail);
        mvwprintw(w, 0, ctx->cols - (int)strlen(p) - 2, "%s", p);
    }
    wattroff(w, COLOR_PAIR(CP_HEADER) | A_BOLD);
    wrefresh(w);
}

static void draw_footer(AppCtx *ctx, const char *hints) {
    WINDOW *w = ctx->win_footer;
    werase(w);
    wattron(w, COLOR_PAIR(CP_FOOTER));
    mvwhline(w, 0, 0, ' ', ctx->cols);
    if (ctx->status_msg[0]) {
        int cp = ctx->status_is_error ? CP_ERROR : CP_FOOTER;
        wattron(w, COLOR_PAIR(cp) | (ctx->status_is_error ? A_BOLD : 0));
        mvwprintw(w, 0, 1, " %s", ctx->status_msg);
        wattroff(w, COLOR_PAIR(cp) | A_BOLD);
    } else {
        mvwprintw(w, 0, 1, " %s", hints);
    }
    wattroff(w, COLOR_PAIR(CP_FOOTER));
    wrefresh(w);
}

static void draw_browser(AppCtx *ctx) {
    WINDOW *w  = ctx->win_browser;
    int h, wd;
    getmaxyx(w, h, wd);
    werase(w);

    /* title row */
    wattron(w, COLOR_PAIR(CP_TITLE) | A_BOLD);
    mvwprintw(w, 0, 1, "Files");
    wattroff(w, COLOR_PAIR(CP_TITLE) | A_BOLD);
    int flag_col = 8;
    if (ctx->show_hidden) {
        wattron(w, COLOR_PAIR(CP_DIM) | A_DIM);
        mvwprintw(w, 0, flag_col, "[hidden]");
        wattroff(w, COLOR_PAIR(CP_DIM) | A_DIM);
        flag_col += 9;
    }
    if (ctx->show_media_only) {
        wattron(w, COLOR_PAIR(CP_VIDEO) | A_BOLD);
        mvwprintw(w, 0, flag_col, "[media]");
        wattroff(w, COLOR_PAIR(CP_VIDEO) | A_BOLD);
    }
    mvwhline(w, 1, 0, ACS_HLINE, wd);

    /* Reserve bottom row for filter bar when active */
    int filter_row = h - 1;
    int list_h = filter_row - 2;   /* rows available for file list */
    if (list_h < 1) list_h = 1;

    /* Adjust scroll */
    if (ctx->browser_cursor < ctx->browser_scroll)
        ctx->browser_scroll = ctx->browser_cursor;
    if (ctx->browser_cursor >= ctx->browser_scroll + list_h)
        ctx->browser_scroll = ctx->browser_cursor - list_h + 1;

    for (int i = 0; i < list_h; i++) {
        int idx = ctx->browser_scroll + i;
        if (idx >= ctx->file_count) break;
        FileEntry *fe = &BFILE(ctx, idx);
        int row = 2 + i;
        int selected = (idx == ctx->browser_cursor);

        if (selected) wattron(w, COLOR_PAIR(CP_SELECTED) | A_BOLD);

        /* type indicator */
        char indicator = ' ';
        int  cp = CP_TITLE;
        if (fe->is_dir)               { indicator = '/'; cp = CP_DIR; }
        else if (fe->type == FILE_TYPE_VIDEO) { indicator = 'V'; cp = CP_VIDEO; }
        else if (fe->type == FILE_TYPE_AUDIO) { indicator = 'A'; cp = CP_AUDIO; }
        else if (fe->type == FILE_TYPE_IMAGE) { indicator = 'I'; cp = CP_SPECIAL; }

        if (!selected) wattron(w, COLOR_PAIR(cp) | (fe->is_dir ? A_BOLD : 0));
        mvwprintw(w, row, 1, "%c ", indicator);
        if (!selected) wattroff(w, COLOR_PAIR(cp) | A_BOLD);

        /* filename — truncated if needed */
        char name_buf[NAME_MAX + 1];
        snprintf(name_buf, sizeof(name_buf), "%s", fe->name);
        int name_max = wd - 14;
        if ((int)strlen(name_buf) > name_max && name_max > 3) {
            name_buf[name_max - 3] = '.';
            name_buf[name_max - 2] = '.';
            name_buf[name_max - 1] = '.';
            name_buf[name_max]     = '\0';
        }
        mvwprintw(w, row, 3, "%-*s", name_max > 0 ? name_max : 1, name_buf);

        /* size (right-aligned) */
        if (!fe->is_dir && !selected) {
            char sz[16];
            format_size(fe->size, sz, sizeof(sz));
            mvwprintw(w, row, wd - (int)strlen(sz) - 1, "%s", sz);
        }

        if (selected) wattroff(w, COLOR_PAIR(CP_SELECTED) | A_BOLD);
    }

    /* filter bar (always occupies bottom row of the browser panel) */
    wattron(w, COLOR_PAIR(ctx->filter_active ? CP_CUSTOM : CP_DIM) | A_BOLD);
    mvwhline(w, filter_row, 0, ' ', wd);
    if (ctx->filter_active) {
        mvwprintw(w, filter_row, 1, " / %s_", ctx->filter_buf);
    } else if (ctx->filter_len > 0) {
        /* filter applied but bar is inactive — show result count */
        mvwprintw(w, filter_row, 1, " /%s  (%d match%s)",
                  ctx->filter_buf,
                  ctx->file_count, ctx->file_count == 1 ? "" : "es");
    } else {
        /* no filter — show scroll position or '/' hint */
        if (ctx->file_count > list_h)
            mvwprintw(w, filter_row, 1, " (%d/%d)  [/] filter",
                      ctx->browser_cursor + 1, ctx->file_count);
        else
            mvwprintw(w, filter_row, 1, " [/] filter");
    }
    wattroff(w, COLOR_PAIR(ctx->filter_active ? CP_CUSTOM : CP_DIM) | A_BOLD);

    wrefresh(w);
}

static void draw_info_panel(AppCtx *ctx) {
    WINDOW *w = ctx->win_info;
    int h, wd;
    getmaxyx(w, h, wd);
    (void)h;
    werase(w);

    mvwvline(w, 0, 0, ACS_VLINE, h);

    wattron(w, COLOR_PAIR(CP_TITLE) | A_BOLD);
    mvwprintw(w, 0, 2, "Info");
    wattroff(w, COLOR_PAIR(CP_TITLE) | A_BOLD);
    mvwhline(w, 1, 1, ACS_HLINE, wd - 1);

    if (ctx->file_count == 0 || ctx->browser_cursor >= ctx->file_count) {
        mvwprintw(w, 3, 2, "No files.");
        wrefresh(w);
        return;
    }

    FileEntry *fe = &BFILE(ctx, ctx->browser_cursor);
    int row = 2;

#define ILABEL(lbl) do { \
    wattron(w, COLOR_PAIR(CP_DIM) | A_DIM); \
    mvwprintw(w, row, 2, "%-12s", lbl); \
    wattroff(w, COLOR_PAIR(CP_DIM) | A_DIM); \
} while(0)
#define IVALUE(...) do { \
    wattron(w, COLOR_PAIR(CP_INFO)); \
    wprintw(w, __VA_ARGS__); \
    wattroff(w, COLOR_PAIR(CP_INFO)); \
    row++; \
} while(0)
#define IROW(lbl, ...) do { ILABEL(lbl); IVALUE(__VA_ARGS__); } while(0)

    if (fe->is_dir) {
        wattron(w, COLOR_PAIR(CP_DIR) | A_BOLD);
        mvwprintw(w, row++, 2, "[Directory]");
        wattroff(w, COLOR_PAIR(CP_DIR) | A_BOLD);
        mvwprintw(w, row++, 2, "%s", fe->name);
        wrefresh(w);
        return;
    }

    /* filename */
    {
        char tmp[NAME_MAX + 1];
        snprintf(tmp, sizeof(tmp), "%s", fe->name);
        int mx = wd - 3;
        if ((int)strlen(tmp) > mx && mx > 3) {
            tmp[mx-3]='.'; tmp[mx-2]='.'; tmp[mx-1]='.'; tmp[mx]='\0';
        }
        wattron(w, A_BOLD);
        mvwprintw(w, row++, 2, "%s", tmp);
        wattroff(w, A_BOLD);
    }
    row++;

    if (!ctx->probe_loaded) {
        wattron(w, COLOR_PAIR(CP_DIM) | A_DIM);
        mvwprintw(w, row, 2, "Probing...");
        wattroff(w, COLOR_PAIR(CP_DIM) | A_DIM);
        wrefresh(w);
        return;
    }

    const ProbeResult *pr = &ctx->probe;
    if (!pr->valid) {
        wattron(w, COLOR_PAIR(CP_DIM));
        mvwprintw(w, row, 2, "Not a media file");
        wattroff(w, COLOR_PAIR(CP_DIM));
        char sz[16]; format_size(fe->size, sz, sizeof(sz));
        row++;
        IROW("Size:", "%s", sz);
        wrefresh(w);
        return;
    }

    /* container / format */
    {
        char dur[16], sz[16], br[16];
        format_duration(pr->duration, dur, sizeof(dur));
        format_size(pr->size, sz, sizeof(sz));
        format_bitrate(pr->bitrate, br, sizeof(br));

        IROW("Container:", "%s", pr->format_name);
        IROW("Duration:",  "%s", dur);
        IROW("Size:",      "%s", sz);
        IROW("Bitrate:",   "%s", br);
    }

    if (pr->has_video) {
        row++;
        wattron(w, COLOR_PAIR(CP_VIDEO) | A_BOLD);
        mvwprintw(w, row++, 2, "─ Video ─");
        wattroff(w, COLOR_PAIR(CP_VIDEO) | A_BOLD);

        char br[16]; format_bitrate(pr->v_bitrate, br, sizeof(br));
        IROW("Codec:",      "%s", pr->v_codec);
        IROW("Resolution:", "%dx%d", pr->v_width, pr->v_height);
        if (pr->v_fps > 0)
            IROW("Frame rate:", "%.2f fps", (double)pr->v_fps);
        if (pr->v_bitrate > 0)
            IROW("Bit rate:", "%s", br);
    }

    if (pr->has_audio) {
        row++;
        wattron(w, COLOR_PAIR(CP_AUDIO) | A_BOLD);
        mvwprintw(w, row++, 2, "─ Audio ─");
        wattroff(w, COLOR_PAIR(CP_AUDIO) | A_BOLD);

        char br[16]; format_bitrate(pr->a_bitrate, br, sizeof(br));
        IROW("Codec:",       "%s", pr->a_codec);
        IROW("Sample rate:", "%d Hz", pr->a_sample_rate);
        IROW("Channels:",    "%d", pr->a_channels);
        if (pr->a_bitrate > 0)
            IROW("Bit rate:", "%s", br);
    }

#undef ILABEL
#undef IVALUE
#undef IROW

    wrefresh(w);
}

/* ── Generic centred popup helper ─────────────────────────────────────── */

static WINDOW *popup_new(AppCtx *ctx, int ph, int pw) {
    int py = (ctx->rows - ph) / 2;
    int px = (ctx->cols - pw) / 2;
    if (py < 0) py = 0;
    if (px < 0) px = 0;
    WINDOW *w = newwin(ph, pw, py, px);
    keypad(w, TRUE);
    return w;
}

static void popup_title(WINDOW *w, int pw, const char *title) {
    wattron(w, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwhline(w, 0, 0, ' ', pw);
    int tl = (int)strlen(title);
    int tx = (pw - tl) / 2;
    if (tx < 1) tx = 1;
    mvwprintw(w, 0, tx, "%s", title);
    wattroff(w, COLOR_PAIR(CP_HEADER) | A_BOLD);
}

static void popup_footer(WINDOW *w, int ph, int pw, const char *hint) {
    wattron(w, COLOR_PAIR(CP_FOOTER));
    mvwhline(w, ph - 1, 0, ' ', pw);
    mvwprintw(w, ph - 1, 2, " %s", hint);
    wattroff(w, COLOR_PAIR(CP_FOOTER));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCREEN: File menu
 * ═══════════════════════════════════════════════════════════════════════════ */

static const char *FILE_MENU_ITEMS[] = {
    "  Presets               apply a built-in or custom preset",
    "  Custom Settings       choose codec, quality, resolution by hand",
    "  Capabilities          show ffmpeg / hardware info",
    "  Back",
    NULL
};

static void draw_file_menu(AppCtx *ctx) {
    int n = 0;
    while (FILE_MENU_ITEMS[n]) n++;

    int pw = 62, ph = n + 6;
    if (ph > ctx->rows - 2) ph = ctx->rows - 2;

    WINDOW *w = popup_new(ctx, ph, pw);
    box(w, 0, 0);
    popup_title(w, pw, " Actions ");

    /* show selected filename */
    const char *fn = strrchr(ctx->selected_file, '/');
    fn = fn ? fn + 1 : ctx->selected_file;
    wattron(w, A_DIM);
    mvwprintw(w, 1, 2, "File: ");
    wattroff(w, A_DIM);
    char tmp[NAME_MAX + 1];
    snprintf(tmp, sizeof(tmp), "%s", fn);
    int max_bytes = pw - 10;
    if ((int)strlen(tmp) > max_bytes && max_bytes > 3) {
        /* truncate UTF-8 safely: back up until we're at a character boundary */
        int i = max_bytes - 3;
        while (i > 0 && ((unsigned char)tmp[i] & 0xC0) == 0x80) i--;
        tmp[i] = tmp[i+1] = tmp[i+2] = '.';
        tmp[i+3] = '\0';
    }
    wattron(w, A_BOLD); wprintw(w, "%s", tmp); wattroff(w, A_BOLD);
    mvwhline(w, 2, 1, ACS_HLINE, pw - 2);

    for (int i = 0; i < n && i + 3 < ph - 1; i++) {
        int row = 3 + i;
        if (i == ctx->menu_cursor) {
            wattron(w, COLOR_PAIR(CP_SELECTED) | A_BOLD);
            mvwhline(w, row, 1, ' ', pw - 2);
            mvwprintw(w, row, 1, "%s", FILE_MENU_ITEMS[i]);
            wattroff(w, COLOR_PAIR(CP_SELECTED) | A_BOLD);
        } else {
            mvwprintw(w, row, 1, "%s", FILE_MENU_ITEMS[i]);
        }
    }

    popup_footer(w, ph, pw, "[Enter] Select  [q/ESC] Back");
    wrefresh(w);
    delwin(w);
}

static void handle_file_menu_input(AppCtx *ctx, int ch) {
    int n = 0;
    while (FILE_MENU_ITEMS[n]) n++;

    switch (ch) {
    case KEY_UP:  case 'k': if (ctx->menu_cursor > 0) ctx->menu_cursor--; break;
    case KEY_DOWN:case 'j': if (ctx->menu_cursor < n-1) ctx->menu_cursor++; break;
    case '\n': case KEY_ENTER:
        switch (ctx->menu_cursor) {
        case 0: ctx->preset_cursor = 0; ctx->preset_scroll = 0;
                ctx->state = STATE_PRESET_MENU; break;
        case 1: ctx->state = STATE_CUSTOM; break;
        case 2: /* capabilities — shown inline */
                ctx->menu_cursor = 0; /* stay in menu, draw caps popup */ break;
        case 3: ctx->state = STATE_BROWSER; break;
        }
        break;
    case 'q': case 'Q': case 27: case KEY_BACKSPACE:
        ctx->state = STATE_BROWSER;
        break;
    }

    /* Show capabilities popup for item 2 */
    if (ch == '\n' || ch == KEY_ENTER) {
        if (ctx->menu_cursor == 2 && ctx->state == STATE_FILE_MENU) {
            Capabilities *c = &ctx->caps;
            int pw = 56, ph = 18;
            WINDOW *w = popup_new(ctx, ph, pw);
            box(w, 0, 0);
            popup_title(w, pw, " ffmpeg Capabilities ");
            int y = 1;
            mvwprintw(w, y++, 2, "Binary:   %s", c->ffmpeg_path);
            mvwprintw(w, y++, 2, "Version:  %s", c->ffmpeg_version);
            y++;
            wattron(w, COLOR_PAIR(CP_TITLE) | A_BOLD);
            mvwprintw(w, y++, 2, "Hardware Encoders:");
            wattroff(w, COLOR_PAIR(CP_TITLE) | A_BOLD);
#define HWROW(label, flag) do { \
    wattron(w, COLOR_PAIR(CP_DIM)); mvwprintw(w, y, 4, "%-20s", label); wattroff(w, COLOR_PAIR(CP_DIM)); \
    if (flag) { wattron(w, COLOR_PAIR(CP_SPECIAL)|A_BOLD); wprintw(w, "yes"); wattroff(w, COLOR_PAIR(CP_SPECIAL)|A_BOLD); } \
    else      { wattron(w, A_DIM); wprintw(w, "no");  wattroff(w, A_DIM); } \
    y++; } while(0)
            HWROW("VideoToolbox (macOS)",  c->has_videotoolbox);
            HWROW("NVENC (NVIDIA)",        c->has_nvenc);
            HWROW("VAAPI (Linux)",         c->has_vaapi);
            HWROW("QSV (Intel)",           c->has_qsv);
#undef HWROW
            y++;
            wattron(w, COLOR_PAIR(CP_TITLE) | A_BOLD);
            mvwprintw(w, y++, 2, "Software Encoders:");
            wattroff(w, COLOR_PAIR(CP_TITLE) | A_BOLD);
#define SWROW(label, flag) do { \
    wattron(w, COLOR_PAIR(CP_DIM)); mvwprintw(w, y, 4, "%-20s", label); wattroff(w, COLOR_PAIR(CP_DIM)); \
    if (flag) { wattron(w, COLOR_PAIR(CP_INFO)|A_BOLD); wprintw(w, "yes"); wattroff(w, COLOR_PAIR(CP_INFO)|A_BOLD); } \
    else      { wattron(w, A_DIM); wprintw(w, "no");  wattroff(w, A_DIM); } \
    y++; } while(0)
            SWROW("libx264",   c->has_libx264);
            SWROW("libx265",   c->has_libx265);
            SWROW("aac",       c->has_aac);
            SWROW("libmp3lame",c->has_libmp3lame);
            SWROW("libopus",   c->has_libopus);
#undef SWROW
            popup_footer(w, ph, pw, "[any key] Close");
            wrefresh(w);
            wtimeout(w, -1);
            wgetch(w);
            delwin(w);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCREEN: Preset menu
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_preset_menu(AppCtx *ctx) {
    int h, wd;
    getmaxyx(stdscr, h, wd);

    WINDOW *w = newwin(h, wd, 0, 0);
    keypad(w, TRUE);

    werase(w);
    wattron(w, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwhline(w, 0, 0, ' ', wd);
    mvwprintw(w, 0, 2, " nfm — Select Preset");
    wattroff(w, COLOR_PAIR(CP_HEADER) | A_BOLD);

    /* legend */
    wattron(w, COLOR_PAIR(CP_VIDEO)   | A_BOLD); mvwprintw(w, 1, 2,  "[V] Video");   wattroff(w, COLOR_PAIR(CP_VIDEO)   | A_BOLD);
    wattron(w, COLOR_PAIR(CP_AUDIO)   | A_BOLD); mvwprintw(w, 1, 14, "[A] Audio");   wattroff(w, COLOR_PAIR(CP_AUDIO)   | A_BOLD);
    wattron(w, COLOR_PAIR(CP_SPECIAL) | A_BOLD); mvwprintw(w, 1, 26, "[S] Special"); wattroff(w, COLOR_PAIR(CP_SPECIAL) | A_BOLD);
    mvwhline(w, 2, 0, ACS_HLINE, wd);

    int list_h = h - 4;
    if (ctx->preset_cursor < ctx->preset_scroll)
        ctx->preset_scroll = ctx->preset_cursor;
    if (ctx->preset_cursor >= ctx->preset_scroll + list_h / 2)
        ctx->preset_scroll = ctx->preset_cursor - list_h / 2 + 1;
    if (ctx->preset_scroll < 0) ctx->preset_scroll = 0;

    const char *last_type = "";
    int row = 3;

    for (int i = ctx->preset_scroll; i < ctx->preset_count && row < h - 1; i++) {
        Preset *p = &ctx->presets[i];

        /* type group header */
        if (strcmp(p->type, last_type) != 0) {
            last_type = p->type;
            if (row >= h - 1) break;
            wattron(w, COLOR_PAIR(p->color_pair) | A_BOLD);
            mvwprintw(w, row, 1, "─── %s ", p->type);
            wattroff(w, COLOR_PAIR(p->color_pair) | A_BOLD);
            wattron(w, COLOR_PAIR(CP_DIM));
            whline(w, ACS_HLINE, wd - 7 - (int)strlen(p->type));
            wattroff(w, COLOR_PAIR(CP_DIM));
            row++;
            if (row >= h - 1) break;
        }

        int selected = (i == ctx->preset_cursor);

        if (selected) {
            wattron(w, COLOR_PAIR(CP_SELECTED) | A_BOLD);
            mvwhline(w, row, 0, ' ', wd);
        } else {
            wattron(w, COLOR_PAIR(p->color_pair));
        }

        mvwprintw(w, row, 3, "%s %s",
                  selected ? ">" : " ",
                  p->name);
        if (p->estimate_savings) {
            wattron(w, COLOR_PAIR(selected ? CP_SELECTED : CP_SPECIAL));
            wprintw(w, " [estimate]");
        }

        if (selected) {
            wattroff(w, COLOR_PAIR(CP_SELECTED) | A_BOLD);
        } else {
            wattroff(w, COLOR_PAIR(p->color_pair));
        }
        row++;

        /* description line */
        if (row < h - 1 && p->description[0]) {
            wattron(w, A_DIM);
            mvwprintw(w, row, 5, "%s", p->description);
            wattroff(w, A_DIM);
            row++;
        }
    }

    /* footer */
    wattron(w, COLOR_PAIR(CP_FOOTER));
    mvwhline(w, h - 1, 0, ' ', wd);
    mvwprintw(w, h - 1, 1, " [Enter] Select  [q/ESC] Back  [k/j] Navigate");
    wattroff(w, COLOR_PAIR(CP_FOOTER));

    wrefresh(w);
    delwin(w);
}

static int show_savings_dialog(AppCtx *ctx, const Preset *p) {
    /* Returns 1 to proceed, 0 to cancel */
    float pct    = estimate_savings(&ctx->probe, p->args);
    long long in_sz = ctx->probe.size;
    long long est   = (pct > 0 && in_sz > 0) ? (long long)(in_sz * (1.0f - pct)) : 0;

    char in_buf[16], est_buf[16];
    format_size(in_sz,  in_buf,  sizeof(in_buf));
    format_size(est,    est_buf, sizeof(est_buf));

    int pw = 54, ph = 12;
    WINDOW *w = popup_new(ctx, ph, pw);
    box(w, 0, 0);
    popup_title(w, pw, " Estimated Output Size ");

    int y = 2;
    wattron(w, COLOR_PAIR(CP_DIM)); mvwprintw(w, y, 2, "Input codec:");  wattroff(w, COLOR_PAIR(CP_DIM));
    wattron(w, A_BOLD);             mvwprintw(w, y, 16, "%s %dx%d",
        ctx->probe.v_codec, ctx->probe.v_width, ctx->probe.v_height);
    wattroff(w, A_BOLD);            y++;

    wattron(w, COLOR_PAIR(CP_DIM)); mvwprintw(w, y, 2, "Input size:");   wattroff(w, COLOR_PAIR(CP_DIM));
    wattron(w, A_BOLD);             mvwprintw(w, y, 16, "%s", in_buf);   wattroff(w, A_BOLD); y++;
    y++;

    wattron(w, COLOR_PAIR(CP_DIM)); mvwprintw(w, y, 2, "Target codec:"); wattroff(w, COLOR_PAIR(CP_DIM));
    wattron(w, COLOR_PAIR(CP_VIDEO)|A_BOLD);
    if      (strstr(p->args, "libx265")) mvwprintw(w, y, 16, "H.265 / HEVC");
    else if (strstr(p->args, "libx264")) mvwprintw(w, y, 16, "H.264 / AVC");
    else if (strstr(p->args, "av1"))     mvwprintw(w, y, 16, "AV1");
    else                                  mvwprintw(w, y, 16, "see preset args");
    wattroff(w, COLOR_PAIR(CP_VIDEO)|A_BOLD); y++;

    if (est > 0) {
        wattron(w, COLOR_PAIR(CP_DIM)); mvwprintw(w, y, 2, "Est. output:"); wattroff(w, COLOR_PAIR(CP_DIM));
        wattron(w, COLOR_PAIR(CP_SPECIAL)|A_BOLD);
        mvwprintw(w, y, 16, "~%s (~%.0f%% smaller)", est_buf, (double)pct * 100.0);
        wattroff(w, COLOR_PAIR(CP_SPECIAL)|A_BOLD); y++;
    } else {
        wattron(w, A_DIM); mvwprintw(w, y, 2, "(savings estimate not available)"); wattroff(w, A_DIM); y++;
    }

    y++;
    wattron(w, A_BOLD); mvwprintw(w, y, 2, "Proceed with encoding?"); wattroff(w, A_BOLD);

    popup_footer(w, ph, pw, "[Enter/y] Yes  [n/q/ESC] No");
    wrefresh(w);

    wtimeout(w, -1);
    int ch = wgetch(w);
    delwin(w);
    return (ch == '\n' || ch == KEY_ENTER || ch == 'y' || ch == 'Y');
}

static void handle_preset_menu_input(AppCtx *ctx, int ch) {
    int n = ctx->preset_count;

    switch (ch) {
    case KEY_UP:   case 'k':
        if (ctx->preset_cursor > 0) ctx->preset_cursor--; break;
    case KEY_DOWN: case 'j':
        if (ctx->preset_cursor < n - 1) ctx->preset_cursor++; break;
    case KEY_PPAGE: ctx->preset_cursor -= 5; if (ctx->preset_cursor < 0) ctx->preset_cursor = 0; break;
    case KEY_NPAGE: ctx->preset_cursor += 5; if (ctx->preset_cursor >= n) ctx->preset_cursor = n - 1; break;
    case KEY_HOME:  ctx->preset_cursor = 0; break;
    case KEY_END:   ctx->preset_cursor = n - 1; break;

    case '\n': case KEY_ENTER: {
        if (n == 0) break;
        Preset *p = &ctx->presets[ctx->preset_cursor];

        /* check input_ext restriction */
        if (p->input_ext[0]) {
            const char *dot = strrchr(ctx->selected_file, '.');
            if (dot) {
                char ext[16] = {0};
                for (int i = 0; dot[i+1] && i < 14; i++) ext[i] = (char)tolower((unsigned char)dot[i+1]);
                if (strcmp(ext, p->input_ext) != 0) {
                    /* show mismatch message */
                    int pw = 50, ph = 5;
                    WINDOW *mw = popup_new(ctx, ph, pw);
                    box(mw, 0, 0);
                    mvwprintw(mw, 2, 2, "This preset requires .%s input files.", p->input_ext);
                    popup_footer(mw, ph, pw, "[any key] OK");
                    wrefresh(mw); wtimeout(mw, -1); wgetch(mw); delwin(mw);
                    break;
                }
            }
        }

        /* savings estimate */
        if (p->estimate_savings && ctx->probe.valid && ctx->probe.has_video) {
            if (!show_savings_dialog(ctx, p)) break;
        }

        /* build output path */
        char outname[64];
        snprintf(outname, sizeof(outname), "%s", p->name);
        /* sanitise for filename */
        for (char *c = outname; *c; c++)
            if (*c == ' ' || *c == '/' || *c == '\\') *c = '_';

        make_output_path(ctx->selected_file, outname, p->ext,
                         ctx->output_file, sizeof(ctx->output_file));

        ctx->input_size_result = ctx->probe.size;
        double dur = ctx->probe.duration;

        int ret = run_encoding(ctx, ctx->selected_file, p->args,
                               ctx->output_file, dur);

        /* Rebuild windows after run_encoding takes over stdscr */
        destroy_windows(ctx);
        create_windows(ctx);
        clearok(stdscr, TRUE);
        refresh();

        if (ret == 0) {
            ctx->state = STATE_RESULT;
        } else {
            snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                     "Encoding failed or cancelled.");
            ctx->status_is_error = 1;
            ctx->state = STATE_BROWSER;
        }
        break;
    }
    case 'q': case 'Q': case 27: case KEY_BACKSPACE:
        ctx->state = STATE_FILE_MENU;
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCREEN: Custom settings
 * ═══════════════════════════════════════════════════════════════════════════ */

static const char *VC_OPTS[] = { "libx264","libx265","copy","none", NULL };
static const char *AC_OPTS[] = { "aac","libmp3lame","libopus","copy","none", NULL };
static const char *SP_OPTS[] = { "medium","slow","fast","veryfast","veryslow", NULL };
static const char *RS_OPTS[] = { "Original","4K (2160p)","1080p","720p","480p", NULL };
static const char *CT_OPTS[] = { "auto","mp4","mkv","webm","m4a","mp3", NULL };
static const char *AB_OPTS[] = { "128k","192k","256k","320k", NULL };

static const char *FIELD_LABELS[] = {
    "Video Codec   ",
    "Quality (CRF) ",
    "Encoder Speed ",
    "Resolution    ",
    "Container     ",
    "Audio Codec   ",
    "Audio Bitrate ",
    NULL
};
#define N_CUSTOM_FIELDS 7

static int field_n(int field) {
    int n = 0;
    switch (field) {
    case 0: while (VC_OPTS[n]) n++; return n;
    case 1: return 52;   /* CRF 0-51 */
    case 2: while (SP_OPTS[n]) n++; return n;
    case 3: while (RS_OPTS[n]) n++; return n;
    case 4: while (CT_OPTS[n]) n++; return n;
    case 5: while (AC_OPTS[n]) n++; return n;
    case 6: while (AB_OPTS[n]) n++; return n;
    }
    return 1;
}

static const char *field_val(const CustomSettings *cs, int field) {
    static char crf_buf[8];
    switch (field) {
    case 0: return VC_OPTS[cs->v_codec_idx];
    case 1: snprintf(crf_buf, sizeof(crf_buf), "%d", cs->crf); return crf_buf;
    case 2: return SP_OPTS[cs->speed_idx];
    case 3: return RS_OPTS[cs->res_idx];
    case 4: return CT_OPTS[cs->container_idx];
    case 5: return AC_OPTS[cs->a_codec_idx];
    case 6: return AB_OPTS[cs->a_bitrate_idx];
    }
    return "";
}

static void field_change(CustomSettings *cs, int field, int delta) {
    int n = field_n(field);
    switch (field) {
    case 0: cs->v_codec_idx   = (cs->v_codec_idx   + delta + n) % n; break;
    case 1: cs->crf            = cs->crf + delta;
            if (cs->crf < 0) cs->crf = 0;
            if (cs->crf > 51) cs->crf = 51; break;
    case 2: cs->speed_idx     = (cs->speed_idx     + delta + n) % n; break;
    case 3: cs->res_idx       = (cs->res_idx       + delta + n) % n; break;
    case 4: cs->container_idx = (cs->container_idx + delta + n) % n; break;
    case 5: cs->a_codec_idx   = (cs->a_codec_idx   + delta + n) % n; break;
    case 6: cs->a_bitrate_idx = (cs->a_bitrate_idx + delta + n) % n; break;
    }
}

/* Build ffmpeg args from custom settings */
static void build_custom_args(const CustomSettings *cs, char *out, size_t n,
                               char *ext_out, size_t ext_n) {
    char tmp[NFM_ARGS_BUFSIZE] = {0};

    /* video codec */
    const char *vc = VC_OPTS[cs->v_codec_idx];
    if (strcmp(vc, "none") == 0)       strncat(tmp, "-vn ", n - strlen(tmp) - 1);
    else if (strcmp(vc, "copy") == 0)  strncat(tmp, "-c:v copy ", n - strlen(tmp) - 1);
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "-c:v %s ", vc);
        strncat(tmp, buf, n - strlen(tmp) - 1);
        /* CRF */
        snprintf(buf, sizeof(buf), "-crf %d ", cs->crf);
        strncat(tmp, buf, n - strlen(tmp) - 1);
        /* preset speed */
        snprintf(buf, sizeof(buf), "-preset %s ", SP_OPTS[cs->speed_idx]);
        strncat(tmp, buf, n - strlen(tmp) - 1);
    }

    /* resolution */
    const char *res = RS_OPTS[cs->res_idx];
    if (strcmp(res, "Original") != 0) {
        int h = 0;
        if      (strcmp(res, "4K (2160p)") == 0) h = 2160;
        else if (strcmp(res, "1080p") == 0)       h = 1080;
        else if (strcmp(res, "720p")  == 0)       h = 720;
        else if (strcmp(res, "480p")  == 0)       h = 480;
        if (h > 0) {
            char buf[32]; snprintf(buf, sizeof(buf), "-vf scale=-2:%d ", h);
            strncat(tmp, buf, n - strlen(tmp) - 1);
        }
    }

    /* audio codec */
    const char *ac = AC_OPTS[cs->a_codec_idx];
    if (strcmp(ac, "none") == 0)       strncat(tmp, "-an ", n - strlen(tmp) - 1);
    else if (strcmp(ac, "copy") == 0)  strncat(tmp, "-c:a copy ", n - strlen(tmp) - 1);
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "-c:a %s ", ac);
        strncat(tmp, buf, n - strlen(tmp) - 1);
        snprintf(buf, sizeof(buf), "-b:a %s ", AB_OPTS[cs->a_bitrate_idx]);
        strncat(tmp, buf, n - strlen(tmp) - 1);
    }

    /* trim trailing space */
    int l = (int)strlen(tmp);
    if (l > 0 && tmp[l-1] == ' ') tmp[l-1] = '\0';

    snprintf(out, n, "%s", tmp);

    /* determine extension */
    const char *ct = CT_OPTS[cs->container_idx];
    if (strcmp(ct, "auto") == 0) {
        if      (strcmp(ac, "none") == 0 && strcmp(vc, "none") != 0) snprintf(ext_out, ext_n, "mkv");
        else if (strcmp(ac, "libmp3lame") == 0)                       snprintf(ext_out, ext_n, "mp3");
        else if (strcmp(ac, "aac") == 0 && strcmp(vc, "none") == 0)   snprintf(ext_out, ext_n, "m4a");
        else                                                           snprintf(ext_out, ext_n, "mp4");
    } else {
        snprintf(ext_out, ext_n, "%s", ct);
    }
}

static void draw_custom_settings(AppCtx *ctx) {
    int h, wd;
    getmaxyx(stdscr, h, wd);

    WINDOW *w = newwin(h, wd, 0, 0);
    keypad(w, TRUE);
    werase(w);

    wattron(w, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwhline(w, 0, 0, ' ', wd);
    mvwprintw(w, 0, 2, " nfm — Custom Encoding Settings");
    wattroff(w, COLOR_PAIR(CP_HEADER) | A_BOLD);

    /* filename */
    const char *fn = strrchr(ctx->selected_file, '/');
    fn = fn ? fn + 1 : ctx->selected_file;
    wattron(w, A_DIM); mvwprintw(w, 1, 2, "File: "); wattroff(w, A_DIM);
    wattron(w, A_BOLD); wprintw(w, "%s", fn); wattroff(w, A_BOLD);
    mvwhline(w, 2, 0, ACS_HLINE, wd);

    int y = 3;
    for (int i = 0; i < N_CUSTOM_FIELDS; i++) {
        int sel = (i == ctx->custom.cursor);
        const char *lbl = FIELD_LABELS[i];
        const char *val = field_val(&ctx->custom, i);

        if (sel) wattron(w, COLOR_PAIR(CP_SELECTED) | A_BOLD);
        else     wattron(w, COLOR_PAIR(CP_CUSTOM));

        mvwprintw(w, y, 2, "%s", lbl);
        if (!sel) { wattroff(w, COLOR_PAIR(CP_CUSTOM)); wattron(w, A_BOLD); }
        wprintw(w, "< %-16s >", val);
        if (sel) wattroff(w, COLOR_PAIR(CP_SELECTED) | A_BOLD);
        else     wattroff(w, A_BOLD);

        if (sel) {
            /* brief hint */
            const char *hint = "";
            if (i == 1) hint = "  (0 = lossless, 23 = default, 51 = worst)";
            if (i == 0) hint = "  [h/l or </> to change]";
            wattron(w, A_DIM); mvwprintw(w, y, 50, "%s", hint); wattroff(w, A_DIM);
        }
        y++;
    }

    y++;
    mvwhline(w, y++, 0, ACS_HLINE, wd);

    /* command preview */
    char args[NFM_ARGS_BUFSIZE], ext[8];
    build_custom_args(&ctx->custom, args, sizeof(args), ext, sizeof(ext));

    char out_path[PATH_MAX];
    make_output_path(ctx->selected_file, "custom", ext, out_path, sizeof(out_path));
    const char *out_fn = strrchr(out_path, '/'); out_fn = out_fn ? out_fn + 1 : out_path;

    wattron(w, COLOR_PAIR(CP_DIM)); mvwprintw(w, y++, 2, "Command preview:"); wattroff(w, COLOR_PAIR(CP_DIM));

    char cmd_line[1024];
    snprintf(cmd_line, sizeof(cmd_line), "ffmpeg -y -i \"%s\" %s \"%s\"", fn, args, out_fn);
    int cmd_max = wd - 4;
    if ((int)strlen(cmd_line) > cmd_max && cmd_max > 3)
        cmd_line[cmd_max] = '\0';
    wattron(w, A_DIM); mvwprintw(w, y++, 2, "%s", cmd_line); wattroff(w, A_DIM);

    wattron(w, COLOR_PAIR(CP_FOOTER));
    mvwhline(w, h-1, 0, ' ', wd);
    mvwprintw(w, h-1, 1, " [Enter] Start  [k/j] Field  [h/l or </> ] Value  [q/ESC] Back");
    wattroff(w, COLOR_PAIR(CP_FOOTER));

    wrefresh(w);
    delwin(w);
}

static void handle_custom_input(AppCtx *ctx, int ch) {
    switch (ch) {
    case KEY_UP:   case 'k': if (ctx->custom.cursor > 0) ctx->custom.cursor--; break;
    case KEY_DOWN: case 'j': if (ctx->custom.cursor < N_CUSTOM_FIELDS-1) ctx->custom.cursor++; break;
    case KEY_LEFT: case 'h': field_change(&ctx->custom, ctx->custom.cursor, -1); break;
    case KEY_RIGHT:case 'l': field_change(&ctx->custom, ctx->custom.cursor, +1); break;
    case '+':                field_change(&ctx->custom, ctx->custom.cursor, +1); break;
    case '-':                field_change(&ctx->custom, ctx->custom.cursor, -1); break;

    case '\n': case KEY_ENTER: {
        char args[NFM_ARGS_BUFSIZE], ext[8];
        build_custom_args(&ctx->custom, args, sizeof(args), ext, sizeof(ext));
        make_output_path(ctx->selected_file, "custom", ext,
                         ctx->output_file, sizeof(ctx->output_file));
        ctx->input_size_result = ctx->probe.size;
        double dur = ctx->probe.duration;

        int ret = run_encoding(ctx, ctx->selected_file, args, ctx->output_file, dur);

        destroy_windows(ctx);
        create_windows(ctx);
        clearok(stdscr, TRUE);
        refresh();

        if (ret == 0) ctx->state = STATE_RESULT;
        else {
            snprintf(ctx->status_msg, sizeof(ctx->status_msg), "Encoding failed or cancelled.");
            ctx->status_is_error = 1;
            ctx->state = STATE_BROWSER;
        }
        break;
    }
    case 'q': case 'Q': case 27: case KEY_BACKSPACE:
        ctx->state = STATE_FILE_MENU;
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCREEN: Result
 * ═══════════════════════════════════════════════════════════════════════════ */

static void draw_result_screen(AppCtx *ctx) {
    int h, wd;
    getmaxyx(stdscr, h, wd);

    WINDOW *w = newwin(h, wd, 0, 0);
    keypad(w, TRUE);
    werase(w);

    wattron(w, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwhline(w, 0, 0, ' ', wd);
    mvwprintw(w, 0, 2, " nfm — Encoding Complete!");
    wattroff(w, COLOR_PAIR(CP_HEADER) | A_BOLD);

    int y = 2;
    wattron(w, COLOR_PAIR(CP_SPECIAL) | A_BOLD);
    mvwprintw(w, y++, 2, "[OK] Encoding finished successfully.");
    wattroff(w, COLOR_PAIR(CP_SPECIAL) | A_BOLD);
    y++;

    const char *out_fn = strrchr(ctx->output_file, '/');
    out_fn = out_fn ? out_fn + 1 : ctx->output_file;

    wattron(w, COLOR_PAIR(CP_DIM)); mvwprintw(w, y, 2, "Output file:  "); wattroff(w, COLOR_PAIR(CP_DIM));
    wattron(w, A_BOLD);             mvwprintw(w, y, 16, "%s", out_fn); wattroff(w, A_BOLD); y++;

    wattron(w, COLOR_PAIR(CP_DIM)); mvwprintw(w, y, 2, "Directory:    "); wattroff(w, COLOR_PAIR(CP_DIM));
    {
        char dir[PATH_MAX]; snprintf(dir, sizeof(dir), "%s", ctx->output_file);
        char *sl = strrchr(dir, '/'); if (sl) *sl = '\0';
        mvwprintw(w, y, 16, "%s", dir);
    }
    y += 2;

    char in_buf[16], out_buf[16];
    format_size(ctx->input_size_result,  in_buf,  sizeof(in_buf));
    format_size(ctx->output_size_result, out_buf, sizeof(out_buf));

    wattron(w, COLOR_PAIR(CP_DIM)); mvwprintw(w, y, 2, "Input size:   "); wattroff(w, COLOR_PAIR(CP_DIM));
    mvwprintw(w, y, 16, "%s", in_buf); y++;

    wattron(w, COLOR_PAIR(CP_DIM)); mvwprintw(w, y, 2, "Output size:  "); wattroff(w, COLOR_PAIR(CP_DIM));
    wattron(w, COLOR_PAIR(CP_SPECIAL) | A_BOLD); mvwprintw(w, y, 16, "%s", out_buf); wattroff(w, COLOR_PAIR(CP_SPECIAL) | A_BOLD);
    if (ctx->input_size_result > 0 && ctx->output_size_result > 0
        && ctx->output_size_result < ctx->input_size_result) {
        double saving = 100.0 * (1.0 - (double)ctx->output_size_result / ctx->input_size_result);
        wattron(w, COLOR_PAIR(CP_SPECIAL));
        wprintw(w, "  (%.1f%% smaller)", saving);
        wattroff(w, COLOR_PAIR(CP_SPECIAL));
    }
    y++;

    if (ctx->encoding_elapsed > 0) {
        wattron(w, COLOR_PAIR(CP_DIM)); mvwprintw(w, y, 2, "Elapsed time: "); wattroff(w, COLOR_PAIR(CP_DIM));
        char dur[16]; format_duration(ctx->encoding_elapsed, dur, sizeof(dur));
        mvwprintw(w, y, 16, "%s", dur);
        y++;
    }

    y += 2;
    wattron(w, COLOR_PAIR(CP_FOOTER));
    mvwhline(w, h-1, 0, ' ', wd);
    mvwprintw(w, h-1, 1, " [Enter/q] Back to browser  [o] Open output directory");
    wattroff(w, COLOR_PAIR(CP_FOOTER));

    wrefresh(w);

    /* input */
    wtimeout(w, -1);
    int ch = wgetch(w);
    if (ch == 'o' || ch == 'O') {
        /* open output directory */
        char dir[PATH_MAX]; snprintf(dir, sizeof(dir), "%s", ctx->output_file);
        char *sl = strrchr(dir, '/'); if (sl) *sl = '\0';
        char cmd[PATH_MAX + 32];
#if defined(__APPLE__)
        snprintf(cmd, sizeof(cmd), "open '%s'", dir);
#else
        snprintf(cmd, sizeof(cmd), "xdg-open '%s' &");
#endif
        system(cmd);
        /* wait for another key */
        wtimeout(w, -1);
        wgetch(w);
    }

    delwin(w);
    ctx->state = STATE_BROWSER;
    /* reset status */
    ctx->status_msg[0]  = '\0';
    ctx->status_is_error = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Inline rename
 * ═══════════════════════════════════════════════════════════════════════════ */

static void do_rename(AppCtx *ctx) {
    if (ctx->file_count == 0) return;
    FileEntry *fe = &BFILE(ctx, ctx->browser_cursor);
    if (strcmp(fe->name, "..") == 0) return;   /* can't rename parent */

    char buf[NAME_MAX + 1];
    snprintf(buf, sizeof(buf), "%s", fe->name);
    int len = (int)strlen(buf);
    int cur = len;   /* byte cursor — starts at end */

    int pw = 64, ph = 7;
    WINDOW *w = popup_new(ctx, ph, pw);
    curs_set(1);
    wtimeout(w, -1);

    for (;;) {
        werase(w);
        box(w, 0, 0);
        popup_title(w, pw, " Rename ");

        /* original name (dimmed) */
        wattron(w, A_DIM);
        char old_disp[NAME_MAX + 1];
        snprintf(old_disp, (size_t)(pw - 10), "%s", fe->name);
        mvwprintw(w, 2, 2, "Old: %s", old_disp);
        wattroff(w, A_DIM);
        mvwhline(w, 3, 1, ACS_HLINE, pw - 2);

        /* editable input field — scroll so cursor is always visible */
        wattron(w, COLOR_PAIR(CP_CUSTOM) | A_BOLD);
        mvwhline(w, 4, 1, ' ', pw - 2);
        int field_w = pw - 4;
        int view_start = (cur >= field_w) ? cur - field_w + 1 : 0;
        char disp[NAME_MAX + 1];
        snprintf(disp, sizeof(disp), "%s", buf + view_start);
        if ((int)strlen(disp) > field_w) disp[field_w] = '\0';
        mvwprintw(w, 4, 2, "%-*s", field_w, disp);
        wattroff(w, COLOR_PAIR(CP_CUSTOM) | A_BOLD);

        popup_footer(w, ph, pw, "[Enter] Rename  [ESC] Cancel");
        wmove(w, 4, 2 + (cur - view_start));
        wrefresh(w);

        int ch = wgetch(w);

        if (ch == 27) break;   /* ESC → cancel */

        if (ch == '\n' || ch == KEY_ENTER) {
            if (len == 0) break;
            if (strcmp(buf, fe->name) == 0) break;   /* no change */
            /* reject names containing '/' */
            if (strchr(buf, '/') != NULL) {
                snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                         "Invalid name: '/' not allowed.");
                ctx->status_is_error = 1;
                break;
            }
            char newpath[PATH_MAX];
            snprintf(newpath, sizeof(newpath), "%s/%s", ctx->current_path, buf);
            if (rename(fe->fullpath, newpath) == 0) {
                snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                         "Renamed to: %s", buf);
                ctx->status_is_error = 0;
                load_directory(ctx);
                ctx->probe_loaded = 0;
            } else {
                snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                         "Rename failed: %s", strerror(errno));
                ctx->status_is_error = 1;
            }
            break;
        }

        /* Backspace — UTF-8-aware delete before cursor */
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (cur > 0) {
                int i = cur - 1;
                while (i > 0 && ((unsigned char)buf[i] & 0xC0) == 0x80) i--;
                memmove(buf + i, buf + cur, (size_t)(len - cur + 1));
                len -= (cur - i);
                cur  = i;
            }
            continue;
        }

        /* Delete — UTF-8-aware delete at cursor */
        if (ch == KEY_DC) {
            if (cur < len) {
                int i = cur + 1;
                while (i < len && ((unsigned char)buf[i] & 0xC0) == 0x80) i++;
                memmove(buf + cur, buf + i, (size_t)(len - i + 1));
                len -= (i - cur);
            }
            continue;
        }

        /* Cursor movement */
        if (ch == KEY_LEFT) {
            if (cur > 0) {
                cur--;
                while (cur > 0 && ((unsigned char)buf[cur] & 0xC0) == 0x80) cur--;
            }
            continue;
        }
        if (ch == KEY_RIGHT) {
            if (cur < len) {
                cur++;
                while (cur < len && ((unsigned char)buf[cur] & 0xC0) == 0x80) cur++;
            }
            continue;
        }
        if (ch == KEY_HOME) { cur = 0;   continue; }
        if (ch == KEY_END)  { cur = len; continue; }

        /* Printable byte — insert at cursor */
        if (ch > 0 && ch < KEY_MIN && ch != 127) {
            if (len < NAME_MAX - 1) {
                memmove(buf + cur + 1, buf + cur, (size_t)(len - cur + 1));
                buf[cur] = (char)(unsigned char)ch;
                cur++;
                len++;
            }
            continue;
        }
    }

    curs_set(0);
    delwin(w);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCREEN: Browser input
 * ═══════════════════════════════════════════════════════════════════════════ */

static void handle_browser_input(AppCtx *ctx, int ch) {
    int h, wd;
    getmaxyx(ctx->win_browser, h, wd);
    (void)wd;
    int list_h = h - 3;   /* -2 title/divider, -1 filter bar */
    if (list_h < 1) list_h = 1;

    /* ── Filter mode intercepts most keys ─────────────────────────────── */
    if (ctx->filter_active) {
        /* ESC: close bar (filter text stays, files remain filtered) */
        if (ch == 27) {
            ctx->filter_active = 0;
            return;
        }
        /* Backspace: delete last UTF-8 codepoint; empty → close bar */
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (ctx->filter_len == 0) {
                ctx->filter_active = 0;
            } else {
                filter_backspace(ctx->filter_buf, &ctx->filter_len);
                apply_filter(ctx);
                ctx->browser_cursor = 0;
                ctx->browser_scroll = 0;
            }
            return;
        }
        /* Arrow navigation and PgUp/PgDn pass through to the switch below */
        if (ch == KEY_UP || ch == KEY_DOWN ||
            ch == KEY_PPAGE || ch == KEY_NPAGE ||
            ch == KEY_HOME  || ch == KEY_END) {
            /* fall through */
        }
        /* Enter: close bar, then open selected item (fall through) */
        else if (ch == '\n' || ch == KEY_ENTER) {
            ctx->filter_active = 0;
            /* fall through */
        }
        /* Any printable byte (ASCII or UTF-8 byte sequence) → add to filter */
        else if (ch > 0 && ch < KEY_MIN && ch != 127) {
            if (ctx->filter_len < (int)sizeof(ctx->filter_buf) - 2) {
                ctx->filter_buf[ctx->filter_len++] = (char)(unsigned char)ch;
                ctx->filter_buf[ctx->filter_len]   = '\0';
            }
            apply_filter(ctx);
            ctx->browser_cursor = 0;
            ctx->browser_scroll = 0;
            return;
        }
    }

    /* ── Normal browser keys ──────────────────────────────────────────── */
    switch (ch) {
    case KEY_UP:   case 'k':
        if (ctx->browser_cursor > 0) {
            ctx->browser_cursor--;
            ctx->probe_loaded = 0;
        }
        break;
    case KEY_DOWN: case 'j':
        if (ctx->browser_cursor < ctx->file_count - 1) {
            ctx->browser_cursor++;
            ctx->probe_loaded = 0;
        }
        break;
    case KEY_PPAGE:
        ctx->browser_cursor -= list_h;
        if (ctx->browser_cursor < 0) ctx->browser_cursor = 0;
        ctx->probe_loaded = 0;
        break;
    case KEY_NPAGE:
        ctx->browser_cursor += list_h;
        if (ctx->browser_cursor >= ctx->file_count) ctx->browser_cursor = ctx->file_count - 1;
        ctx->probe_loaded = 0;
        break;
    case KEY_HOME: ctx->browser_cursor = 0; ctx->probe_loaded = 0; break;
    case KEY_END:  ctx->browser_cursor = ctx->file_count - 1; ctx->probe_loaded = 0; break;

    case '\n': case KEY_ENTER: {
        if (ctx->file_count == 0) break;
        FileEntry *fe = &BFILE(ctx, ctx->browser_cursor);
        if (fe->is_dir) {
            if (strcmp(fe->name, "..") == 0) {
                char *sl = strrchr(ctx->current_path, '/');
                if (sl && sl != ctx->current_path) *sl = '\0';
                else if (sl == ctx->current_path) { ctx->current_path[1] = '\0'; }
            } else {
                char newpath[PATH_MAX];
                snprintf(newpath, sizeof(newpath), "%s/%s", ctx->current_path, fe->name);
                snprintf(ctx->current_path, sizeof(ctx->current_path), "%s", newpath);
            }
            load_directory(ctx);
            ctx->probe_loaded = 0;
        } else if (fe->type == FILE_TYPE_VIDEO || fe->type == FILE_TYPE_AUDIO
                   || fe->type == FILE_TYPE_IMAGE) {
            snprintf(ctx->selected_file, sizeof(ctx->selected_file), "%s", fe->fullpath);
            ctx->selected_type = fe->type;
            ctx->menu_cursor   = 0;
            ctx->state         = STATE_FILE_MENU;
        }
        break;
    }

    /* Backspace / Left arrow: go up a directory (only when filter bar is closed) */
    case KEY_BACKSPACE: case 127: case 8: case KEY_LEFT: {
        if (ctx->filter_active) break; /* already handled above */
        char *sl = strrchr(ctx->current_path, '/');
        if (sl && sl != ctx->current_path) *sl = '\0';
        else if (sl == ctx->current_path && ctx->current_path[1] != '\0')
            ctx->current_path[1] = '\0';
        load_directory(ctx);
        ctx->probe_loaded = 0;
        break;
    }

    case '/':
        /* open filter bar */
        ctx->filter_active = 1;
        break;

    case 27: /* ESC when bar not active: clear filter entirely */
        if (!ctx->filter_active && ctx->filter_len > 0) {
            ctx->filter_buf[0] = '\0';
            ctx->filter_len    = 0;
            apply_filter(ctx);
            ctx->browser_cursor = 0;
            ctx->browser_scroll = 0;
        }
        break;

    case '.':
        ctx->show_hidden = !ctx->show_hidden;
        load_directory(ctx);
        ctx->probe_loaded = 0;
        break;

    case 'm': case 'M':
        ctx->show_media_only = !ctx->show_media_only;
        load_directory(ctx);
        ctx->probe_loaded = 0;
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 ctx->show_media_only ? "Showing media files only." : "Showing all files.");
        ctx->status_is_error = 0;
        break;

    case KEY_F(2): case 'e': case 'E':
        do_rename(ctx);
        break;

    case 'r': case 'R':
        load_directory(ctx);
        ctx->probe_loaded = 0;
        snprintf(ctx->status_msg, sizeof(ctx->status_msg), "Refreshed.");
        ctx->status_is_error = 0;
        break;

    case 'q': case 'Q':
        ctx->state = STATE_QUIT;
        break;

    case '?': {
        /* quick help popup */
        int pw = 52, ph = 19;
        WINDOW *hw = popup_new(ctx, ph, pw);
        box(hw, 0, 0);
        popup_title(hw, pw, " Key Bindings ");
        int y = 1;
        mvwprintw(hw, y++, 2, "Up/Dn  k/j    Navigate files");
        mvwprintw(hw, y++, 2, "Left  Backsp  Go up one directory");
        mvwprintw(hw, y++, 2, "Enter         Enter dir / open file");
        mvwprintw(hw, y++, 2, "PgUp/PgDn     Scroll quickly");
        mvwprintw(hw, y++, 2, "Home/End      First/last file");
        mvwprintw(hw, y++, 2, "/             Filter as you type");
        mvwprintw(hw, y++, 2, "F2 / e        Rename file or folder");
        mvwprintw(hw, y++, 2, ".             Toggle hidden files");
        mvwprintw(hw, y++, 2, "m             Toggle media-only view");
        mvwprintw(hw, y++, 2, "r             Refresh directory");
        mvwprintw(hw, y++, 2, "q             Quit");
        mvwprintw(hw, y++, 2, "?             This help");
        popup_footer(hw, ph, pw, "[any key] Close");
        wrefresh(hw); wtimeout(hw, -1); wgetch(hw); delwin(hw);
        break;
    }

    default:
        /* clear status on any other key */
        if (ctx->status_msg[0] && ch != ERR) {
            ctx->status_msg[0] = '\0';
            ctx->status_is_error = 0;
        }
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Install dialog (ffmpeg not found)
 * ═══════════════════════════════════════════════════════════════════════════ */

static int show_install_dialog(AppCtx *ctx) {
    int pw = 62, ph = 12;
    WINDOW *w = popup_new(ctx, ph, pw);
    box(w, 0, 0);
    popup_title(w, pw, " ffmpeg Not Found ");

    mvwprintw(w, 2, 2, "ffmpeg and/or ffprobe were not found in PATH.");
    mvwprintw(w, 3, 2, "nfm requires ffmpeg to convert media files.");
    mvwprintw(w, 5, 2, "Would you like to install ffmpeg now?");
#if defined(__APPLE__)
    mvwprintw(w, 6, 4, "(via Homebrew: brew install ffmpeg)");
#else
    mvwprintw(w, 6, 4, "(via apt: sudo apt-get install -y ffmpeg)");
#endif
    popup_footer(w, ph, pw, "[y] Install  [n] Continue without  [q] Quit");
    wrefresh(w);

    wtimeout(w, -1);
    int ch = wgetch(w);
    delwin(w);

    if (ch == 'q' || ch == 'Q') return -1;
    if (ch == 'y' || ch == 'Y') {
        endwin();
        int ok = offer_install_ffmpeg();
        initscr();
        cbreak(); noecho(); curs_set(0); keypad(stdscr, TRUE);
        init_colors();
        if (ok) {
            detect_ffmpeg(&ctx->caps);
            detect_capabilities(&ctx->caps);
        }
        return ok ? 1 : 0;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * main()
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    /* Must be called before initscr() so ncurses uses UTF-8 — fixes garbled
     * multi-byte characters (■ ✓ ↑ ↓) and umlauts in filenames. */
    setlocale(LC_ALL, "");

    AppCtx ctx;
    memset(&ctx, 0, sizeof(ctx));

    /* Determine starting directory */
    if (argc > 1) {
        struct stat st;
        if (stat(argv[1], &st) == 0 && S_ISDIR(st.st_mode))
            snprintf(ctx.current_path, sizeof(ctx.current_path), "%s", argv[1]);
        else
            getcwd(ctx.current_path, sizeof(ctx.current_path));
    } else {
        getcwd(ctx.current_path, sizeof(ctx.current_path));
    }

    /* Initialise custom settings defaults */
    ctx.custom.crf = 23;

    /* Detect ffmpeg */
    detect_ffmpeg(&ctx.caps);

    /* Initialise ncurses */
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    init_colors();

    /* SIGWINCH for terminal resize */
#ifdef SIGWINCH
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigwinch_handler;
    sigaction(SIGWINCH, &sa, NULL);
#endif

    create_windows(&ctx);

    /* Offer to install ffmpeg if missing */
    if (!ctx.caps.ffmpeg_found || !ctx.caps.ffprobe_found) {
        int r = show_install_dialog(&ctx);
        if (r < 0) { /* quit */ goto cleanup; }
        if (!ctx.caps.ffmpeg_found) {
            snprintf(ctx.status_msg, sizeof(ctx.status_msg),
                     "ffmpeg not found — encoding disabled.");
            ctx.status_is_error = 1;
        }
    }

    /* Detect capabilities (codecs, hardware) */
    detect_capabilities(&ctx.caps);

    /* Ensure user preset dir exists and load presets */
    init_user_preset_dir();
    load_presets(&ctx.presets, &ctx.preset_count, &ctx.caps);

    /* Load initial directory */
    load_directory(&ctx);
    ctx.state = STATE_BROWSER;

    /* ── Main loop ─────────────────────────────────────────────────────── */
    while (ctx.state != STATE_QUIT) {
        if (g_resize) handle_resize(&ctx);

        switch (ctx.state) {
        case STATE_BROWSER:
            /* Auto-probe selected file if not yet done */
            if (!ctx.probe_loaded && ctx.file_count > 0) {
                FileEntry *fe = &BFILE(&ctx, ctx.browser_cursor);
                if (!fe->is_dir && (fe->type == FILE_TYPE_VIDEO ||
                                    fe->type == FILE_TYPE_AUDIO ||
                                    fe->type == FILE_TYPE_IMAGE)) {
                    probe_file(fe->fullpath, &ctx.caps, &ctx.probe);
                } else {
                    memset(&ctx.probe, 0, sizeof(ctx.probe));
                }
                ctx.probe_loaded = 1;
            }
            draw_header(&ctx);
            draw_browser(&ctx);
            draw_info_panel(&ctx);
            draw_footer(&ctx, "[k/j] Navigate  [Enter] Open  [Back] Up  [.] Hidden  [m] Media only  [?] Help  [q] Quit");
            {
                int ch = wgetch(ctx.win_browser);
                if (ch != ERR) handle_browser_input(&ctx, ch);
            }
            break;

        case STATE_FILE_MENU:
            draw_header(&ctx);
            draw_browser(&ctx);
            draw_info_panel(&ctx);
            draw_footer(&ctx, "[k/j] Navigate  [Enter] Select  [q/ESC] Back");
            draw_file_menu(&ctx);
            {
                int ch = wgetch(ctx.win_browser);
                if (ch != ERR) handle_file_menu_input(&ctx, ch);
            }
            break;

        case STATE_PRESET_MENU:
            draw_preset_menu(&ctx);
            {
                WINDOW *tmp = newwin(1, 1, 0, 0);
                keypad(tmp, TRUE);
                wtimeout(tmp, 50);
                int ch = wgetch(tmp);
                delwin(tmp);
                if (ch != ERR) handle_preset_menu_input(&ctx, ch);
            }
            break;

        case STATE_CUSTOM:
            draw_custom_settings(&ctx);
            {
                WINDOW *tmp = newwin(1, 1, 0, 0);
                keypad(tmp, TRUE);
                wtimeout(tmp, 50);
                int ch = wgetch(tmp);
                delwin(tmp);
                if (ch != ERR) handle_custom_input(&ctx, ch);
            }
            break;

        case STATE_RESULT:
            draw_result_screen(&ctx);   /* blocking until key press inside */
            break;

        default:
            break;
        }
    }

cleanup:
    destroy_windows(&ctx);
    endwin();
    free(ctx.all_files);
    free_presets(ctx.presets);
    return 0;
}
