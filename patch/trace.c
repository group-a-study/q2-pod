/* Development-only playback trace (docs/remote.md#trace): while playback cuts out now and then, with
 * the screen off and with headphones or AirPlay alike, a line a second of what the Q2 does, so a cut can
 * be read back afterwards. Each second:
 *   up=  uptime, s; lit= backlight, vol= volume (demo's bytes, read through /proc/<demo>/mem)
 *   cpu= busy % of both cores since the last line, load=, on= (CPUs online), free= (MemAvailable kB)
 *   pcm: each open playback stream's state, delay, avail and hardware and application pointers
 *   hci: each hciplayer thread: state, the kernel function it waits in (wchan) and its CPU %
 *   irq: the audio interrupts (aic, dma, i2s) since the last line
 *   D: each task in uninterruptible sleep, with its wchan
 *   wifi= the Wi-Fi signal
 * and, as they come, the kernel's new messages (K:) and demo's and hciplayer's output (/tmp/q2-demo.log,
 * L:). Kept in RAM and appended to the card every TRACE_FLUSH_S (opened and closed, never kept busy)
 * while it is mounted; TRACE_MAX, then one older file. */
#ifndef CARD
#define CARD "/mnt/mmc"
#endif
#ifndef TRACE_FILE
#define TRACE_FILE CARD "/q2pod-trace.log"
#endif
#ifndef DEMO_LOG
#define DEMO_LOG "/tmp/q2-demo.log"
#endif
#define TRACE_MAX (8 << 20)
#define TRACE_FLUSH_S 10
#define BUF (192 << 10)
#define G_BACKLIGHT 0x99f5b0 /* tools/q2remote.py GLOBALS */
#define G_VOLUME 0xa38c41

#ifdef TRACE_HOST
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define O_LOG (O_WRONLY | O_CREAT | O_APPEND)
#define O_NB (O_RDONLY | O_NONBLOCK)
#define dirent64 dirent
#define readdir64 readdir
#else
#define O_LOG 0x109 /* MIPS O_WRONLY | O_CREAT | O_APPEND */
#define O_NB 0x80   /* MIPS O_RDONLY | O_NONBLOCK */
struct dirent64 { unsigned long long d_ino; long long d_off; unsigned short d_reclen; unsigned char d_type; char d_name[256]; };
struct timespec { long tv_sec, tv_nsec; };
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; long gmtoff; const char *zone; };
int open(const char *, int, ...), read(int, void *, unsigned), write(int, const void *, unsigned), close(int);
long long lseek64(int, long long, int);
int rename(const char *, const char *), usleep(unsigned), snprintf(char *, unsigned, const char *, ...);
void *opendir(const char *); struct dirent64 *readdir64(void *); int closedir(void *);
int clock_gettime(int, struct timespec *); long time(long *); struct tm *gmtime_r(const long *, struct tm *);
int strcmp(const char *, const char *), strncmp(const char *, const char *, unsigned);
char *strstr(const char *, const char *); unsigned strlen(const char *);
void *memcpy(void *, const void *, unsigned);
#endif

static char out[BUF];
static unsigned used;

static void put(const char *s, unsigned n) {
    if (used + n + 1 > BUF) return; /* the card away a long while: the newest are dropped */
    memcpy(out + used, s, n);
    used += n;
}
#define PUTS(s) put(s, strlen(s))

static int slurp(const char *path, char *buf, int cap) {
    int fd = open(path, 0), n = 0;
    if (fd < 0) return -1;
    for (int r; n < cap - 1 && (r = read(fd, buf + n, cap - 1 - n)) > 0;) n += r;
    close(fd);
    buf[n] = 0;
    return n;
}

static long num(const char *s) {
    long v = 0;
    int neg = *s == '-';
    for (s += neg; *s >= '0' && *s <= '9'; ++s) v = v * 10 + *s - '0';
    return neg ? -v : v;
}

/* The value after "key:" in a /proc status text, to the line's end, spaces trimmed. */
static void field(const char *text, const char *key, char *val, int cap) {
    const char *p = strstr(text, key);
    int n = 0;
    if (p) {
        p += strlen(key);
        while (*p == ' ' || *p == '\t' || *p == ':') ++p;
        while (*p && *p != '\n' && n < cap - 1) val[n++] = *p++;
    }
    val[n] = 0;
}

static int card_mounted(void) {
    char m[4096];
    return slurp("/proc/mounts", m, sizeof m) > 0 && strstr(m, " " CARD " ") != 0;
}

static void flush(void) {
    if (!used || !card_mounted()) return;
    int fd = open(TRACE_FILE, 0);
    long long size = fd >= 0 ? lseek64(fd, 0, 2) : 0;
    if (fd >= 0) close(fd);
    if (size > TRACE_MAX) rename(TRACE_FILE, TRACE_FILE ".1");
    fd = open(TRACE_FILE, O_LOG, 0644);
    if (fd < 0) return;
    unsigned done = 0;
    for (int r; done < used && (r = write(fd, out + done, used - done)) > 0;) done += r;
    close(fd);
    used = 0;
}

/* A pid by its comm, 0 without one. */
static int pid_of(const char *comm) {
    void *d = opendir("/proc");
    int pid = 0;
    char path[64], c[32];
    for (struct dirent64 *e; d && !pid && (e = readdir64(d));) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
        if (slurp(path, c, sizeof c) > 0) {
            for (char *q = c; *q; ++q)
                if (*q == '\n') *q = 0;
            if (!strcmp(c, comm)) pid = (int)num(e->d_name);
        }
    }
    if (d) closedir(d);
    return pid;
}

static int peek(int pid, unsigned addr) {
    char path[32];
    unsigned char b = 0;
    snprintf(path, sizeof path, "/proc/%d/mem", pid);
    int fd = open(path, 0), ok = fd >= 0 && lseek64(fd, addr, 0) == addr && read(fd, &b, 1) == 1;
    if (fd >= 0) close(fd);
    return ok ? b : -1;
}

/* A task's state letter and utime + stime, from its stat; 0 when gone. */
static char task_stat(const char *path, long *ticks) {
    char s[512];
    if (slurp(path, s, sizeof s) <= 0) return 0;
    char *p = s, *close_paren = 0;
    for (; *p; ++p)
        if (*p == ')') close_paren = p;
    if (!close_paren) return 0;
    p = close_paren + 2; /* field 3 */
    char state = *p;
    for (int f = 3; f < 14 && *p; ++p)
        if (*p == ' ') ++f;
    long u = num(p);
    while (*p && *p != ' ') ++p;
    *ticks = u + num(p + 1);
    return state;
}

#define TASKS 32
static struct { int tid; long ticks; } seen[TASKS];

static long last_ticks(int tid, long now) {
    for (int i = 0; i < TASKS; ++i)
        if (seen[i].tid == tid) {
            long old = seen[i].ticks;
            seen[i].ticks = now;
            return old;
        }
    for (int i = 0; i < TASKS; ++i)
        if (!seen[i].tid) {
            seen[i].tid = tid, seen[i].ticks = now;
            break;
        }
    return now;
}

static void hci(int pid, char *line, unsigned cap, unsigned *n) {
    char dir[48], path[96], w[48];
    snprintf(dir, sizeof dir, "/proc/%d/task", pid);
    void *d = opendir(dir);
    for (struct dirent64 *e; d && (e = readdir64(d));) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        long ticks = 0;
        snprintf(path, sizeof path, "%s/%s/stat", dir, e->d_name);
        char state = task_stat(path, &ticks);
        if (!state) continue;
        snprintf(path, sizeof path, "%s/%s/wchan", dir, e->d_name);
        if (slurp(path, w, sizeof w) < 0) w[0] = 0;
        long old = last_ticks((int)num(e->d_name), ticks);
        *n += snprintf(line + *n, cap - *n, " %s:%c:%s:%ld", e->d_name, state, w[0] ? w : "-", ticks - old);
        if (*n >= cap) *n = cap - 1;
    }
    if (d) closedir(d);
}

/* Tasks in uninterruptible sleep, with their wchan: a stuck driver shows here. */
static void dstate(char *line, unsigned cap, unsigned *n) {
    void *d = opendir("/proc");
    char path[64], w[48];
    for (struct dirent64 *e; d && (e = readdir64(d));) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        long ticks;
        snprintf(path, sizeof path, "/proc/%s/stat", e->d_name);
        if (task_stat(path, &ticks) != 'D') continue;
        snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
        char c[24];
        if (slurp(path, c, sizeof c) <= 0) c[0] = 0;
        for (char *q = c; *q; ++q)
            if (*q == '\n') *q = 0;
        snprintf(path, sizeof path, "/proc/%s/wchan", e->d_name);
        if (slurp(path, w, sizeof w) < 0) w[0] = 0;
        *n += snprintf(line + *n, cap - *n, " D:%s/%s:%s", e->d_name, c, w);
        if (*n >= cap) *n = cap - 1;
    }
    if (d) closedir(d);
}

#define IRQS 8
static struct { char name[24]; long count; } irq[IRQS];

static void irqs(char *line, unsigned cap, unsigned *n) {
    static char s[8192];
    if (slurp("/proc/interrupts", s, sizeof s) <= 0) return;
    *n += snprintf(line + *n, cap - *n, " irq");
    for (char *p = s, *end; *p; p = *end ? end + 1 : end) {
        for (end = p; *end && *end != '\n'; ++end) {
        }
        char keep = *end;
        *end = 0;
        int audio = strstr(p, "aic") || strstr(p, "AIC") || strstr(p, "dma") || strstr(p, "DMA") ||
                    strstr(p, "i2s") || strstr(p, "I2S");
        if (audio) {
            const char *q = p;
            while (*q == ' ') ++q;
            long total = 0; /* the per-CPU counts after "N:" */
            while (*q && *q != ':') ++q;
            for (++q;;) {
                while (*q == ' ') ++q;
                if (*q < '0' || *q > '9') break;
                total += num(q);
                while (*q >= '0' && *q <= '9') ++q;
            }
            const char *name = q; /* the controller and the action; the last word names it */
            for (const char *r = q; *r; ++r)
                if (*r == ' ' && r[1] && r[1] != ' ') name = r + 1;
            int i = 0;
            while (i < IRQS && irq[i].name[0] && strcmp(irq[i].name, name)) ++i;
            if (i < IRQS) {
                if (!irq[i].name[0]) snprintf(irq[i].name, sizeof irq[i].name, "%s", name), irq[i].count = total;
                *n += snprintf(line + *n, cap - *n, " %s+%ld", irq[i].name, total - irq[i].count);
                irq[i].count = total;
            }
        }
        *end = keep;
        if (*n >= cap) { *n = cap - 1; break; }
    }
}

static void pcms(char *line, unsigned cap, unsigned *n) {
    char path[96], s[1024], st[24], delay[16], avail[16], hw[16], appl[16];
    for (int card = 0; card < 4; ++card)
        for (int dev = 0; dev < 4; ++dev) {
            snprintf(path, sizeof path, "/proc/asound/card%d/pcm%dp/sub0/status", card, dev);
            if (slurp(path, s, sizeof s) <= 0 || !strncmp(s, "closed", 6)) continue;
            field(s, "state", st, sizeof st);
            field(s, "delay", delay, sizeof delay);
            field(s, "avail", avail, sizeof avail);
            field(s, "hw_ptr", hw, sizeof hw);
            field(s, "appl_ptr", appl, sizeof appl);
            *n += snprintf(line + *n, cap - *n, " pcm%d.%d=%s,d%s,a%s,hw%s,ap%s", card, dev, st, delay, avail, hw, appl);
            if (*n >= cap) *n = cap - 1;
        }
}

/* New lines of a log another process appends to, each as "PREFIX line". */
static void follow(const char *path, long long *at, const char *prefix) {
    char b[8192];
    int fd = open(path, 0);
    if (fd < 0) return;
    long long size = lseek64(fd, 0, 2);
    if (size < *at) *at = 0; /* emptied */
    if (*at < size - (64 << 10)) *at = size - (64 << 10); /* a burst: the last 64 kB */
    lseek64(fd, *at, 0);
    for (int r; (r = read(fd, b, sizeof b)) > 0;) {
        int start = 0;
        for (int i = 0; i < r; ++i)
            if (b[i] == '\n') {
                PUTS(prefix);
                put(b + start, (unsigned)(i - start + 1));
                start = i + 1;
            }
        if (!start && r == (int)sizeof b) { /* one line longer than b: as it is */
            PUTS(prefix);
            put(b, (unsigned)r);
            PUTS("\n");
            start = r;
        }
        *at += start; /* a partial last line waits for its end */
        if (start < r) break;
    }
    close(fd);
}

/* The kernel's new messages, as "K: text" (/dev/kmsg records: "prio,seq,usec,flags;text"). */
static void kmsg(int fd) {
    char rec[1024];
    for (int r; (r = read(fd, rec, sizeof rec - 1)) > 0;) {
        rec[r] = 0;
        char *text = rec;
        while (*text && *text != ';') ++text;
        if (*text) ++text;
        PUTS("K: ");
        PUTS(text);
        if (r && rec[r - 1] != '\n') PUTS("\n");
    }
}

int main(void) {
    long prev_busy = 0, prev_total = 0;
    long long demo_at = 0;
    int demo = 0, player = 0, kfd = open("/dev/kmsg", O_NB);
    if (kfd >= 0) lseek64(kfd, 0, 2); /* from now on */
    int fdl = open(DEMO_LOG, 0);
    if (fdl >= 0) demo_at = lseek64(fdl, 0, 2), close(fdl);
    for (unsigned tick = 0;; ++tick) {
        static char line[4096];
        unsigned n = 0;
        struct timespec ts;
        clock_gettime(1, &ts);
        long now = time(0);
        struct tm tm;
        gmtime_r(&now, &tm);
        if (tick % 10 == 0 || !demo) demo = pid_of("demo");
        if (tick % 10 == 0 || !player) player = pid_of("hciplayer");
        n += snprintf(line + n, sizeof line - n, "%02d:%02d:%02d up=%ld.%03ld lit=%d vol=%d", tm.tm_hour,
                      tm.tm_min, tm.tm_sec, ts.tv_sec, ts.tv_nsec / 1000000,
                      demo ? peek(demo, G_BACKLIGHT) : -1, demo ? peek(demo, G_VOLUME) : -1);
        char s[512], v[64];
        if (slurp("/proc/stat", s, sizeof s) > 0) { /* "cpu  user nice system idle iowait irq softirq" */
            long f[8] = { 0 }, total = 0;
            const char *p = s + 3;
            for (int i = 0; i < 8; ++i) {
                while (*p == ' ') ++p;
                f[i] = num(p);
                total += f[i];
                while (*p >= '0' && *p <= '9') ++p;
            }
            long busy = total - f[3] - f[4];
            if (prev_total && total > prev_total)
                n += snprintf(line + n, sizeof line - n, " cpu=%ld%%", 100 * (busy - prev_busy) / (total - prev_total));
            prev_busy = busy, prev_total = total;
        }
        if (slurp("/proc/loadavg", s, sizeof s) > 0) {
            int k = 0;
            while (s[k] && s[k] != ' ') ++k;
            s[k] = 0;
            n += snprintf(line + n, sizeof line - n, " load=%s", s);
        }
        if (slurp("/sys/devices/system/cpu/online", v, sizeof v) > 0) {
            for (char *q = v; *q; ++q)
                if (*q == '\n') *q = 0;
            n += snprintf(line + n, sizeof line - n, " on=%s", v);
        }
        static char mem[2048];
        if (slurp("/proc/meminfo", mem, sizeof mem) > 0) {
            field(mem, "MemAvailable", v, sizeof v);
            n += snprintf(line + n, sizeof line - n, " free=%ld", num(v));
        }
        if (slurp("/proc/net/rtl8733bs/wlan0/rx_signal", s, sizeof s) > 0) {
            field(s, "signal_strength", v, sizeof v);
            if (v[0]) n += snprintf(line + n, sizeof line - n, " wifi=%s", v);
        }
        if (n >= sizeof line) n = sizeof line - 1;
        pcms(line, sizeof line, &n);
        if (player) {
            n += snprintf(line + n, sizeof line - n, " hci");
            if (n >= sizeof line) n = sizeof line - 1;
            hci(player, line, sizeof line, &n);
        }
        irqs(line, sizeof line, &n);
        if (tick % 2 == 0) dstate(line, sizeof line, &n);
        if (n >= sizeof line - 1) n = sizeof line - 2;
        line[n++] = '\n';
        put(line, n);
        if (kfd >= 0) kmsg(kfd);
        follow(DEMO_LOG, &demo_at, "L: ");
        if (tick % TRACE_FLUSH_S == TRACE_FLUSH_S - 1) flush();
        usleep(1000000);
    }
}
