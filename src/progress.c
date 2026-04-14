/*
 * progress.c — Fork ffmpeg, display a real-time progress screen, return result
 */
#define _POSIX_C_SOURCE 200809L

#include "nfm.h"
#include "progress.h"

/* ── Globals ───────────────────────────────────────────────────────────── */

static volatile pid_t g_enc_pid = -1;

static void sig_term(int s) {
    (void)s;
    if (g_enc_pid > 0) kill(g_enc_pid, SIGTERM);
}

/* ── Progress-line parser ──────────────────────────────────────────────── */

static void parse_progress(const char *line, ProgressData *p, double total) {
    const char *ptr;

    ptr = strstr(line, "frame=");
    if (ptr) p->frame = atoll(ptr + 6);

    ptr = strstr(line, "fps=");
    if (ptr) p->fps = atof(ptr + 4);

    ptr = strstr(line, "size=");
    if (ptr) p->size_bytes = atoll(ptr + 5) * 1024;

    /* time=HH:MM:SS.xx */
    ptr = strstr(line, "time=");
    if (ptr) {
        int h, m, s, cs;
        if (sscanf(ptr + 5, "%d:%d:%d.%d", &h, &m, &s, &cs) == 4) {
            double cur = h * 3600.0 + m * 60.0 + s + cs / 100.0;
            snprintf(p->time_str, sizeof(p->time_str), "%d:%02d:%02d", h, m, s);
            if (total > 0) {
                p->pct = (cur / total) * 100.0;
                if (p->pct > 100.0) p->pct = 100.0;
                if (p->pct > 0 && p->speed > 0.001) {
                    double rem = (total - cur) / p->speed;
                    int ir = (int)rem;
                    int eh = ir / 3600, em = (ir % 3600) / 60, es = ir % 60;
                    if (eh > 0)
                        snprintf(p->eta_str, sizeof(p->eta_str), "%d:%02d:%02d", eh, em, es);
                    else
                        snprintf(p->eta_str, sizeof(p->eta_str), "%d:%02d", em, es);
                }
            }
        }
    }

    ptr = strstr(line, "bitrate=");
    if (ptr) p->bitrate_kbps = (long long)atof(ptr + 8);

    ptr = strstr(line, "speed=");
    if (ptr) p->speed = atof(ptr + 6);
}

/* ── ncurses draw ──────────────────────────────────────────────────────── */

static void draw_progress(WINDOW *win, const ProgressData *p,
                           const char *in_name, const char *out_name,
                           int rows, int cols) {
    werase(win);

    /* header */
    wattron(win, COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvwhline(win, 0, 0, ' ', cols);
    mvwprintw(win, 0, 2, " nfm — Encoding in progress ");
    wattroff(win, COLOR_PAIR(CP_HEADER) | A_BOLD);

    int y = 2;

    /* filenames */
    int max_fn = cols - 12;
    if (max_fn < 4) max_fn = 4;

#define PRINT_FNAME(label, name) do { \
        wattron(win, COLOR_PAIR(CP_DIM)); \
        mvwprintw(win, y, 2, "%s", (label)); \
        wattroff(win, COLOR_PAIR(CP_DIM)); \
        wattron(win, A_BOLD); \
        char _tmp[512]; \
        snprintf(_tmp, sizeof(_tmp), "%s", (name)); \
        if ((int)strlen(_tmp) > max_fn) { \
            _tmp[max_fn-3]='.'; _tmp[max_fn-2]='.'; \
            _tmp[max_fn-1]='.'; _tmp[max_fn]='\0'; } \
        mvwprintw(win, y, 2 + (int)strlen(label), "%s", _tmp); \
        wattroff(win, A_BOLD); \
        y++; \
    } while(0)

    PRINT_FNAME("Input:  ", in_name);
    PRINT_FNAME("Output: ", out_name);
#undef PRINT_FNAME
    y++;

    /* progress bar */
    int bw = cols - 18;
    if (bw < 10) bw = 10;
    int filled = (int)(p->pct / 100.0 * bw);
    if (filled > bw) filled = bw;

    mvwprintw(win, y, 2, "[");
    wattron(win, COLOR_PAIR(CP_PROGRESS) | A_BOLD);
    for (int i = 0; i < bw; i++)
        waddch(win, i < filled ? '=' : ' ');
    wattroff(win, COLOR_PAIR(CP_PROGRESS) | A_BOLD);
    wprintw(win, "] ");
    wattron(win, A_BOLD);
    wprintw(win, "%5.1f%%", p->pct);
    wattroff(win, A_BOLD);
    y += 2;

    /* stats (two columns) */
    char sz[32];
    format_size(p->size_bytes, sz, sizeof(sz));

    int c2 = cols / 2;

#define STAT_LABEL(row, col, lbl) do { \
    wattron(win, COLOR_PAIR(CP_DIM)); \
    mvwprintw(win, (row), (col), "%s", (lbl)); \
    wattroff(win, COLOR_PAIR(CP_DIM)); } while(0)
#define STAT_VAL(row, col, ...) do { \
    wattron(win, COLOR_PAIR(CP_INFO)); \
    mvwprintw(win, (row), (col), __VA_ARGS__); \
    wattroff(win, COLOR_PAIR(CP_INFO)); } while(0)

    STAT_LABEL(y, 2,    "Time:    ");
    STAT_VAL  (y, 11,   "%-12s", p->time_str[0] ? p->time_str : "--");
    STAT_LABEL(y, c2,   "FPS:     ");
    if (p->fps > 0) STAT_VAL(y, c2+9, "%.1f", p->fps);
    else            STAT_VAL(y, c2+9, "--");
    y++;

    STAT_LABEL(y, 2,    "ETA:     ");
    STAT_VAL  (y, 11,   "%-12s", p->eta_str[0] ? p->eta_str : "--");
    STAT_LABEL(y, c2,   "Speed:   ");
    if (p->speed > 0.001) STAT_VAL(y, c2+9, "%.2fx", p->speed);
    else                  STAT_VAL(y, c2+9, "--");
    y++;

    STAT_LABEL(y, 2,    "Size:    ");
    STAT_VAL  (y, 11,   "%-12s", sz);
    STAT_LABEL(y, c2,   "Bitrate: ");
    if (p->bitrate_kbps > 0) STAT_VAL(y, c2+9, "%lld kbps", p->bitrate_kbps);
    else                     STAT_VAL(y, c2+9, "--");
    y += 2;

#undef STAT_LABEL
#undef STAT_VAL

    /* log lines */
    wattron(win, COLOR_PAIR(CP_DIM));
    mvwprintw(win, y++, 2, "ffmpeg output:");
    wattroff(win, COLOR_PAIR(CP_DIM));

    for (int i = 0; i < p->log_count && y < rows - 2; i++) {
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "%s", p->log[i]);
        if ((int)strlen(tmp) > cols - 4) tmp[cols-4] = '\0';
        wattron(win, A_DIM);
        mvwprintw(win, y++, 2, "%s", tmp);
        wattroff(win, A_DIM);
    }

    /* footer */
    wattron(win, COLOR_PAIR(CP_FOOTER));
    mvwhline(win, rows - 1, 0, ' ', cols);
    mvwprintw(win, rows - 1, 2, " [q / ESC]  Cancel encoding");
    wattroff(win, COLOR_PAIR(CP_FOOTER));

    wrefresh(win);
}

/* ── Argument splitter ─────────────────────────────────────────────────── */
/*
 * Splits a whitespace-separated string into argv[].
 * Respects single- and double-quoted sub-strings so filter expressions
 * like -vf 'fps=15,scale=480:-1' are passed as one argument.
 */
static int split_args(const char *src, char **argv, int max_argc,
                      char *buf, int bufsz) {
    int argc = 0, bpos = 0;
    const char *p = src;

    while (*p && argc < max_argc - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        argv[argc++] = buf + bpos;

        if (*p == '"') {
            p++;
            while (*p && *p != '"' && bpos < bufsz - 2) buf[bpos++] = *p++;
            if (*p == '"') p++;
        } else if (*p == '\'') {
            p++;
            while (*p && *p != '\'' && bpos < bufsz - 2) buf[bpos++] = *p++;
            if (*p == '\'') p++;
        } else {
            while (*p && *p != ' ' && *p != '\t' && bpos < bufsz - 2)
                buf[bpos++] = *p++;
        }
        buf[bpos++] = '\0';
    }
    argv[argc] = NULL;
    return argc;
}

/* ── Public entry point ────────────────────────────────────────────────── */

int run_encoding(AppCtx *ctx, const char *input, const char *extra_args,
                 const char *output, double total_duration) {

    /* Build argv */
    char args_buf[NFM_ARGS_BUFSIZE];
    char *extra_argv[NFM_MAX_ARGS];
    int  extra_argc = split_args(extra_args, extra_argv, NFM_MAX_ARGS,
                                 args_buf, sizeof(args_buf));

    char *argv[NFM_MAX_ARGS + 8];
    int   argc = 0;
    argv[argc++] = ctx->caps.ffmpeg_path;
    argv[argc++] = "-y";
    argv[argc++] = "-i";
    argv[argc++] = (char *)input;
    for (int i = 0; i < extra_argc; i++) argv[argc++] = extra_argv[i];
    argv[argc++] = (char *)output;
    argv[argc]   = NULL;

    /* Pipe for ffmpeg stderr */
    int pfd[2];
    if (pipe(pfd) < 0) return -1;
    fcntl(pfd[0], F_SETFL, fcntl(pfd[0], F_GETFL, 0) | O_NONBLOCK);

    struct timeval tv0;
    gettimeofday(&tv0, NULL);

    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); return -1; }

    if (pid == 0) {
        close(pfd[0]);
        dup2(pfd[1], STDERR_FILENO);
        close(pfd[1]);
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) { dup2(dn, STDOUT_FILENO); close(dn); }
        execv(argv[0], argv);
        _exit(1);
    }

    g_enc_pid = pid;
    close(pfd[1]);

    struct sigaction sa_old, sa_new;
    sa_new.sa_handler = sig_term;
    sigemptyset(&sa_new.sa_mask);
    sa_new.sa_flags = 0;
    sigaction(SIGINT, &sa_new, &sa_old);

    /* Progress window */
    memset(&ctx->progress, 0, sizeof(ctx->progress));
    WINDOW *wprog = newwin(ctx->rows, ctx->cols, 0, 0);
    keypad(wprog, TRUE);
    wtimeout(wprog, 80);

    const char *in_name  = strrchr(input,  '/'); in_name  = in_name  ? in_name  + 1 : input;
    const char *out_name = strrchr(output, '/'); out_name = out_name ? out_name + 1 : output;

    draw_progress(wprog, &ctx->progress, in_name, out_name, ctx->rows, ctx->cols);

    char lbuf[2048];
    int  llen  = 0;
    char cbuf[512];
    int  cancelled = 0;

    for (;;) {
        /* Has child exited? */
        int status;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            ctx->progress.done = 1;
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
                ctx->progress.error = 1;
            if (cancelled)
                ctx->progress.error = 1;
            break;
        }

        /* Read available pipe data */
        ssize_t nr = read(pfd[0], cbuf, sizeof(cbuf) - 1);
        if (nr > 0) {
            for (int i = 0; i < (int)nr; i++) {
                char c = cbuf[i];
                if (c == '\r' || c == '\n') {
                    if (llen > 0) {
                        lbuf[llen] = '\0';
                        if (strstr(lbuf, "time=") || strstr(lbuf, "frame=")) {
                            parse_progress(lbuf, &ctx->progress, total_duration);
                        } else {
                            /* add to rolling log */
                            ProgressData *pd = &ctx->progress;
                            if (pd->log_count < 6) {
                                snprintf(pd->log[pd->log_count++], 256, "%s", lbuf);
                            } else {
                                memmove(pd->log[0], pd->log[1], 256 * 5);
                                snprintf(pd->log[5], 256, "%s", lbuf);
                            }
                        }
                        llen = 0;
                    }
                } else if (llen < (int)sizeof(lbuf) - 2) {
                    lbuf[llen++] = c;
                }
            }
            draw_progress(wprog, &ctx->progress, in_name, out_name, ctx->rows, ctx->cols);
        }

        /* Keyboard */
        int ch = wgetch(wprog);
        if (ch == 'q' || ch == 'Q' || ch == 27) {
            mvwprintw(wprog, ctx->rows - 3, 2, "  Cancel encoding? [y/n]: ");
            wrefresh(wprog);
            wtimeout(wprog, -1);
            int c2 = wgetch(wprog);
            if (c2 == 'y' || c2 == 'Y') {
                cancelled = 1;
                kill(pid, SIGTERM);
                usleep(400000);
                kill(pid, SIGKILL);
            }
            wtimeout(wprog, 80);
        }
    }

    /* Drain remaining pipe data */
    {
        ssize_t nr;
        while ((nr = read(pfd[0], cbuf, sizeof(cbuf) - 1)) > 0) {
            for (int i = 0; i < (int)nr; i++) {
                char c = cbuf[i];
                if (c == '\r' || c == '\n') {
                    if (llen > 0) {
                        lbuf[llen] = '\0';
                        if (strstr(lbuf, "time=") || strstr(lbuf, "frame="))
                            parse_progress(lbuf, &ctx->progress, total_duration);
                        llen = 0;
                    }
                } else if (llen < (int)sizeof(lbuf) - 2) {
                    lbuf[llen++] = c;
                }
            }
        }
    }
    close(pfd[0]);
    waitpid(pid, NULL, 0);
    g_enc_pid = -1;
    sigaction(SIGINT, &sa_old, NULL);

    /* Final display update */
    if (!ctx->progress.error && !cancelled) {
        ctx->progress.pct = 100.0;
        draw_progress(wprog, &ctx->progress, in_name, out_name, ctx->rows, ctx->cols);
        usleep(500000);
    }

    /* Record elapsed time */
    struct timeval tv1;
    gettimeofday(&tv1, NULL);
    ctx->encoding_elapsed = (tv1.tv_sec - tv0.tv_sec) +
                            (tv1.tv_usec - tv0.tv_usec) / 1e6;

    delwin(wprog);

    if (cancelled || ctx->progress.error) {
        unlink(output);
        return -1;
    }

    struct stat st;
    if (stat(output, &st) == 0)
        ctx->output_size_result = st.st_size;

    return 0;
}
