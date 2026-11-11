/* Add an App (docs/internals.md#add-an-app), the App Store's last row (netupdate.c): its page on a
 * phone on the same Wi-Fi, served from a thread while addon.c's QR page shows the address, the PIN
 * and the QR code from PH_STATE (as a card app's status file). A GitHub repo's link pasted there
 * joins SH_MINE once GitHub shows its latest release with a zip, and the App Store lists SH_MINE's
 * after its own: installing is still a press on the Q2. The page removes what it added too. The QR
 * code carries a key that signs the phone in at once; the address typed asks for the PIN. Both are
 * new at each press of the row, and the server ends when the QR page closes. */
#include "peq.h"

#define PH_PORT 8300 /* the first tried, then the next PH_PORTS - 1; card apps take 8000 and 8100  \
                      */
#define PH_PORTS 10
#define PH_STATE "/tmp/q2-apps.state"
#define PH_HEX 640             /* addon.c's QR_HEX: the most of the code's hex its QR page reads */
#define PH_REQ 8192            /* a request's head and form, the most read */
#define PH_PAGE 32768          /* a page */
#define PH_OUT (PH_PAGE + 512) /* a reply: its head, then the page */
#define PH_WAIT_MS 5000u       /* a request whole within it, or dropped; its reply as long */
#define PH_TRIES 5             /* wrong PINs before the page takes none */
#define PH_AWAKE_MS 120000u    /* standby held off this long after the phone's last request */
#define PH_CLIENTS 4           /* connections waiting for their request: a browser opens spares */
#define PH_IDLE_MS 10000u      /* one waiting longer is closed */
#define PH_COOKIE "q2apps="

struct ph_addr { /* sockaddr_in */
    unsigned short family, port;
    unsigned char ip[4];
    char zero[8];
};

static struct {
    unsigned long thread;
    volatile int running, stop, seq, xfer[2]; /* xfer: shelf_lookup's cancel flag and percentage */
    volatile unsigned used;                   /* now_ms's low 32 bits at the phone's last request */
    int fd, port, tries, seen;
    unsigned timer;
    char key[17], pin[7], ip[16], note[96], said[320]; /* said: the page's message, shown once */
} ph __attribute__((section(".scratch")));

/* n characters of alphabet's m, NUL-ended, from /dev/urandom (a clock-seeded generator for what it
 * can't give), each byte past the last whole run of m passed over so none is likelier. */
static void secret(char *out, int n, const char *alphabet, unsigned m) {
    unsigned char b[48];
    int fd = open("/dev/urandom", 0), got = fd >= 0 ? read(fd, b, sizeof b) : 0, k = 0;
    unsigned seed = (unsigned)now_ms() ^ (unsigned)(unsigned long)out;
    if (fd >= 0) close(fd);
    for (int i = 0; i < n; ++k) {
        unsigned v = k < got ? b[k] : (seed = seed * 1103515245u + 12345u) >> 16 & 255;
        if (v < 256 - 256 % m) out[i++] = alphabet[v % m];
    }
    out[n] = 0;
}

/* The Wi-Fi's IPv4 address into out (16 bytes), "" with none. */
static void wifi_ip(char *out) {
    unsigned char r[32] = "wlan0"; /* struct ifreq: the name, then its sockaddr_in */
    int fd = socket(2, 1, 0);      /* AF_INET, SOCK_DGRAM (MIPS numbering) */
    *out = 0;
    if (fd < 0) return;
    if (!ioctl(fd, 0x8915, r) && r[16] == 2 && (r[20] | r[21] | r[22] | r[23])) /* SIOCGIFADDR */
        snprintf(out, 16, "%u.%u.%u.%u", r[20], r[21], r[22], r[23]);
    close(fd);
}

/* PH_STATE for the QR page: the address, the PIN, the code of the address with the key, the note;
 * written again when the Wi-Fi's address (looked up again) changes, or at once with force. */
static void ph_state(int force) {
    char ip[16], url[80], hex[PH_HEX] = "", s[PH_HEX + 256], addr[24] = "";
    int size = 0;
    wifi_ip(ip);
    if (!force && !strcmp(ip, ph.ip)) return;
    memcpy(ph.ip, ip, sizeof ip);
    if (*ip) {
        int n = snprintf(url, sizeof url, "http://%s:%d/?k=%s", ip, ph.port, ph.key);
        size = qr_hex(url, (unsigned)n, -1, hex, sizeof hex);
        snprintf(addr, sizeof addr, "%s:%d", ip, ph.port);
    }
    int n = snprintf(s, sizeof s, "addr=%s\npin=%s\nqr=%s\nqrsize=%d\nnote=%s\n", addr, ph.pin,
                     size ? hex : "", size, ph.note);
    void *f = fopen(PH_STATE ".tmp", "wb");
    int ok = f && n > 0 && fwrite(s, 1, (unsigned)n, f) == (unsigned)n;
    if (f && fclose(f)) ok = 0;
    if (!ok || rename(PH_STATE ".tmp", PH_STATE)) unlink(PH_STATE ".tmp");
}

/* A page as it is written: s, n bytes of cap (one kept for the NUL; what doesn't fit is cut). */
typedef struct {
    char *s;
    unsigned n, cap;
} text_t;

static void say(text_t *t, const char *s) {
    while (*s && t->n + 1 < t->cap) t->s[t->n++] = *s++;
    t->s[t->n] = 0;
}

/* s with HTML's five escaped. */
static void say_html(text_t *t, const char *s) {
    for (char c[2] = { 0 }; *s; ++s) {
        const char *e = *s == '&'    ? "&amp;"
                        : *s == '<'  ? "&lt;"
                        : *s == '>'  ? "&gt;"
                        : *s == '"'  ? "&quot;"
                        : *s == '\'' ? "&#39;"
                                     : 0;
        c[0] = *s;
        say(t, e ? e : c);
    }
}

static int hex_digit(char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}

/* A form's (or a query's) field key, %-decoded and "+" a space, into out (n bytes, cut to fit); 1
 * when it is there. form ends at its NUL. */
static int form_field(const char *form, const char *key, char *out, unsigned n) {
    unsigned k = strlen(key), i = 0;
    for (const char *p = form; p && *p;) {
        if (!strncmp(p, key, k) && p[k] == '=') {
            for (p += k + 1; *p && *p != '&'; ++p) {
                int c = *p == '+' ? ' ' : (unsigned char)*p;
                if (*p == '%' && hex_digit(p[1]) >= 0 && hex_digit(p[2]) >= 0)
                    c = hex_digit(p[1]) << 4 | hex_digit(p[2]), p += 2;
                if (i + 1 < n) out[i++] = (char)c;
            }
            out[i] = 0;
            return 1;
        }
        p = strstr(p, "&");
        p = p ? p + 1 : 0;
    }
    *out = 0;
    return 0;
}

/* What the phone pasted, a GitHub link or owner/repo (".git" and what follows the repo dropped),
 * as owner/repo in lower case (GitHub's are case-blind; the id needs it) into out (80 bytes); 0
 * when it is neither. */
static int repo_of(const char *s, char *out) {
    static const char *const skip[] = { "https://", "http://", "www.", "github.com/" };
    while (*s == ' ' || *s == '\t') ++s;
    for (unsigned k = 0; k < sizeof skip / sizeof *skip; ++k)
        if (!strncasecmp(s, skip[k], strlen(skip[k]))) s += strlen(skip[k]);
    const char *slash = s, *end;
    while (*slash && *slash != '/') ++slash;
    if (!*slash) return 0;
    for (end = slash + 1; *end && *end != '/' && *end != '?' && *end != '#' && *end != ' ';) ++end;
    unsigned on = (unsigned)(slash - s), rn = (unsigned)(end - slash - 1);
    if (rn > 4 && !strncasecmp(end - 4, ".git", 4)) rn -= 4;
    if (!on || on > 39 || !rn || on + 1 + rn >= 80) return 0;
    for (unsigned i = 0; i < on + 1 + rn; ++i) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || i == on || (i > on && (c == '.' || c == '_'))))
            return 0;
        out[i] = c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
    }
    out[on + 1 + rn] = 0;
    return 1;
}

/* A name typed for the Q2 cleaned: no control characters, no spaces at its ends, 31 bytes at most
 * (cut where a UTF-8 character starts), as sources.list's. */
static void name_clean(char *s) {
    char *o = s, *p = s;
    while (*p == ' ') ++p;
    for (; *p; ++p) *o++ = (unsigned char)*p < ' ' || *p == 127 ? ' ' : *p;
    if (o - s > 31)
        for (o = s + 31; o > s && ((unsigned char)*o & 0xc0) == 0x80;) --o;
    while (o > s && o[-1] == ' ') --o;
    *o = 0;
}

/* The name a repo gives when none is typed: its name less "q2-", "-" and "_" as spaces, the first
 * letter a capital ("owner/q2-file-browser": "File browser"). */
static void name_of(const char *repo, char *out, unsigned n) {
    const char *r = strstr(repo, "/") + 1;
    if (!strncmp(r, "q2-", 3) && r[3]) r += 3;
    snprintf(out, n, "%s", r);
    for (char *c = out; *c; ++c)
        if (*c == '-' || *c == '_') *c = ' ';
    if (*out >= 'a' && *out <= 'z') *out = (char)(*out - 32);
    name_clean(out);
}

/* SH_MINE again with line added at its end, or (line 0) repo's lines dropped, how many in *dropped
 * and the last one's name in name (32 bytes): written whole through a .tmp, as the card's other
 * lists; the file gone once empty. Whether it was written. */
static int mine_write(const char *line, const char *repo, char *name, int *dropped) {
    unsigned n = 0, m = 0;
    char *old = read_all(SH_MINE, 1u << 16, &n), *text = calloc(1, n + 200);
    int ok = 0;
    for (const char *p = old, *e; text && p && *p; p = *e ? e + 1 : e) {
        const char *sp = p, *nm;
        e = strstr(p, "\n");
        e = e ? e : p + strlen(p);
        while (sp < e && *sp != ' ' && *sp != '\t') ++sp;
        if (repo && (unsigned)(sp - p) == strlen(repo) &&
            !strncasecmp(p, repo, (unsigned)(sp - p))) {
            for (nm = sp; nm < e && (*nm == ' ' || *nm == '\t'); ++nm) {}
            snprintf(name, 32, "%.*s", (int)(e - nm), nm);
            name_clean(name);
            ++*dropped;
            continue;
        }
        memcpy(text + m, p, (unsigned)(e - p)), m += (unsigned)(e - p);
        if (e > p) text[m++] = '\n';
    }
    if (text && line) memcpy(text + m, line, strlen(line)), m += strlen(line);
    if (text) {
        mkdir("/mnt/mmc/.q2", 0755);
        ok = m ? blob_io(SH_MINE, SH_MINE ".tmp", text, m, 1) : !unlink(SH_MINE) || !n;
    }
    free(old), free(text);
    return ok;
}

/* The form's repo added: checked as sources.list's line, against the App Store's list, then on
 * GitHub; the outcome in said (and, added, the QR page's note). */
static void add(const char *form) {
    char link[256], name[96], repo[80], line[160], tag[32];
    unsigned size = 0;
    shelf_app a, *known = calloc(SH_MAX, sizeof *known);
    int n = known ? shelf_known(known) : 0, k = 0, r;
    form_field(form, "repo", link, sizeof link);
    form_field(form, "name", name, sizeof name);
    name_clean(name);
    if (!known) return;
    if (!repo_of(link, repo)) {
        snprintf(ph.said, sizeof ph.said,
                 "That isn't a GitHub repo's link. Paste one like https://github.com/owner/q2-app");
        goto done;
    }
    if (!*name) name_of(repo, name, 32);
    snprintf(line, sizeof line, "%s %s\n", repo, name);
    if (shelf_sources(line, &a, 1) != 1) {
        snprintf(ph.said, sizeof ph.said,
                 "%s can't be an app here: its card folder is named after the repo less \"q2-\", "
                 "which takes 1 to 15 lower-case letters, digits or -, and none of Q2 Pod's own",
                 repo);
        goto done;
    }
    while (k < n && strcasecmp(known[k].repo, a.repo) && strcmp(known[k].id, a.id)) ++k;
    if (k < n && !strcasecmp(known[k].repo, a.repo))
        snprintf(ph.said, sizeof ph.said, "%s is in the App Store already, as %s", repo,
                 known[k].name);
    else if (k < n)
        snprintf(ph.said, sizeof ph.said,
                 "The App Store has %s (%s) already, whose card folder .%s this one would share",
                 known[k].name, known[k].repo, a.id);
    else if (n >= SH_MAX)
        snprintf(ph.said, sizeof ph.said,
                 "The App Store is full (%d apps): remove one you added first", SH_MAX);
    if (k < n || n >= SH_MAX) goto done;
    ph.xfer[0] = ph.stop, ph.xfer[1] = 0;
    r = shelf_lookup(a.repo, tag, sizeof tag, &size, ph.xfer);
    if (r == SH_OK && !mine_write(line, 0, 0, 0)) r = SH_CARD;
    if (r == SH_OK) {
        unsigned tenths = ((size >> 10) * 10 + 512) >> 10; /* of a MB, rounded */
        snprintf(ph.said, sizeof ph.said,
                 "Added %s (%s, %u.%u MB). On the Q2, press Return: %s has a row in the App "
                 "Store, and pressing it installs it.",
                 a.name, tag, tenths / 10, tenths % 10, a.name);
        snprintf(ph.note, sizeof ph.note, "%s added: press Return, then its row", a.name);
        ++ph.seq;
        ph_state(1);
    } else
        snprintf(ph.said, sizeof ph.said, "%s%s%s", r == SH_BAD ? repo : "",
                 r == SH_MISSING ? "GitHub shows no release of "
                 : r == SH_BAD   ? "'s latest release has no .zip of the app to install"
                 : r == SH_BUSY  ? "GitHub is busy: try again in a while"
                 : r == SH_CARD  ? "Can't write to the card"
                 : r == SH_NET   ? "Can't reach GitHub: is the Q2 still on Wi-Fi?"
                                 : "Stopped",
                 r == SH_MISSING ? repo : "");
    if (r == SH_MISSING)
        snprintf(ph.said + strlen(ph.said), sizeof ph.said - strlen(ph.said),
                 " (or no public repo of that name)");
done:
    free(known);
}

/* The form's repo dropped from SH_MINE; the outcome in said. */
static void drop(const char *form) {
    char link[256], repo[80], name[32] = "";
    int n = 0;
    form_field(form, "repo", link, sizeof link);
    if (!repo_of(link, repo)) return;
    if (!mine_write(0, repo, name, &n))
        snprintf(ph.said, sizeof ph.said, "Can't write to the card");
    else if (!n)
        snprintf(ph.said, sizeof ph.said, "%s isn't on your list", repo);
    else {
        snprintf(ph.said, sizeof ph.said,
                 "Removed %s from the App Store. Installed, it stays on the card until you "
                 "remove it there.",
                 *name ? name : repo);
        snprintf(ph.note, sizeof ph.note, "%s removed", *name ? name : repo);
        ++ph.seq;
        ph_state(1);
    }
}

static const char style[] =
    "<style>:root{color-scheme:light dark;--bg:#f2f2f7;--card:#fff;--ink:#1c1c1e;--dim:#6e6e73;"
    "--line:#d8d8dc;--accent:#0a7aff;--red:#e5342b}"
    "@media(prefers-color-scheme:dark){:root{--bg:#000;--card:#1c1c1e;--ink:#f2f2f7;--dim:#98989f;"
    "--line:#38383a;--accent:#0a84ff;--red:#ff453a}}"
    "body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.4 -apple-system,system-ui,"
    "sans-serif}main{max-width:34em;margin:0 auto;padding:16px}h1{font-size:1.6em;margin:.3em 0 0}"
    "h2{font-size:1em;margin:1.6em 0 .5em}.sub,.note,.all{color:var(--dim)}.note{font-size:.9em}"
    ".said{background:var(--card);border-left:4px solid var(--accent);padding:.6em .8em;"
    "border-radius:8px;overflow-wrap:anywhere}form.box{background:var(--card);padding:12px;"
    "border-radius:12px}label{display:block;font-size:.9em;color:var(--dim);margin:.4em 0 .2em}"
    "input{box-sizing:border-box;width:100%;font:inherit;padding:.55em .6em;border:1px solid "
    "var(--line);border-radius:8px;background:var(--bg);color:var(--ink)}button{font:inherit;"
    "font-weight:600;border:0;border-radius:8px;padding:.55em 1.2em;background:var(--accent);"
    "color:#fff;margin-top:.8em}ul{list-style:none;padding:0;margin:0;background:var(--card);"
    "border-radius:12px}li{display:flex;align-items:center;gap:8px;padding:10px 12px;border-top:"
    "1px solid var(--line)}li:first-child{border-top:0}li div{flex:1;min-width:0;overflow-wrap:"
    "anywhere}li a{display:block;color:var(--dim);font-size:.9em;text-decoration:none}li form "
    "button{margin:0;padding:.3em .2em;background:none;color:var(--red)}</style>";

static void page_head(text_t *t, const char *sub) {
    say(t, "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
           "content=\"width=device-width,initial-scale=1\"><title>Add an App</title>");
    say(t, style);
    say(t, "</head><body><main><h1>Add an App</h1><p class=\"sub\">");
    say(t, sub);
    say(t, "</p>");
    if (*ph.said) say(t, "<p class=\"said\">"), say_html(t, ph.said), say(t, "</p>");
    *ph.said = 0; /* said once */
}

static void page_pin(text_t *t) {
    page_head(t, "Type the PIN the Q2 shows");
    say(t, "<form class=\"box\" method=\"post\" action=\"/pin\"><label for=\"pin\">PIN</label>"
           "<input id=\"pin\" name=\"pin\" inputmode=\"numeric\" autocomplete=\"one-time-code\" "
           "maxlength=\"7\" required autofocus><button>Open</button></form></main></body></html>");
}

/* The page: the form, what it added (each with Remove), the rest of the App Store's list. */
static void page_main(text_t *t) {
    shelf_app *known = calloc(SH_MAX, sizeof *known), *mine = calloc(SH_MAX, sizeof *mine);
    unsigned len = 0;
    char *text = read_all(SH_MINE, 1u << 16, &len);
    int n = known ? shelf_known(known) : 0, m = mine ? shelf_sources(text, mine, SH_MAX) : 0,
        rest = 0;
    free(text);
    page_head(t, "to the Q2's App Store, from its GitHub link");
    say(t, "<form class=\"box\" method=\"post\" action=\"/add\"><label for=\"repo\">GitHub link"
           "</label><input id=\"repo\" name=\"repo\" required placeholder=\"https://github.com/"
           "owner/q2-app\" autocapitalize=\"off\" autocorrect=\"off\" spellcheck=\"false\" "
           "inputmode=\"url\"><label for=\"name\">Name on the Q2 (if not the repo's)</label>"
           "<input id=\"name\" name=\"name\" maxlength=\"31\"><button>Add</button></form>"
           "<p class=\"note\">Its latest release needs the app as a .zip whose files sit as they "
           "go on the card, as Q2 Pod's card apps' do. Once it's added, press Return on the Q2 and "
           "press its row to install it.</p><h2>Added here</h2>");
    say(t, m ? "<ul>" : "<p class=\"all\">None yet</p>");
    for (int k = 0; k < m; ++k) {
        say(t, "<li><div><b>"), say_html(t, mine[k].name);
        say(t, "</b><a href=\"https://github.com/"), say_html(t, mine[k].repo), say(t, "\">");
        say_html(t, mine[k].repo);
        say(t, "</a></div><form method=\"post\" action=\"/remove\"><input type=\"hidden\" "
               "name=\"repo\" value=\"");
        say_html(t, mine[k].repo);
        say(t, "\"><button>Remove</button></form></li>");
    }
    say(t, m ? "</ul><p class=\"note\">Removing one takes it off the App Store's list; installed, "
               "it stays on the card until you remove it there.</p>"
             : "");
    for (int k = 0; k < n; ++k) {
        int own = 0;
        for (int j = 0; j < m; ++j) own |= !strcmp(mine[j].id, known[k].id);
        if (own) continue;
        say(t, rest++ ? ", " : "<h2>In the App Store</h2><p class=\"all\">");
        say_html(t, known[k].name);
    }
    say(t, rest ? "</p></main></body></html>" : "</main></body></html>");
    free(known), free(mine);
}

/* Header field name's value in head (the request's lines, NUL-ended) into out (n bytes); 1 when
 * there. */
static int header(const char *head, const char *name, char *out, unsigned n) {
    unsigned k = strlen(name), i = 0;
    for (const char *p = strstr(head, "\r\n"); p; p = strstr(p, "\r\n")) {
        p += 2;
        if (strncasecmp(p, name, k) || p[k] != ':') continue;
        for (p += k + 1; *p == ' '; ++p) {}
        while (*p && *p != '\r' && i + 1 < n) out[i++] = *p++;
        out[i] = 0;
        return 1;
    }
    *out = 0;
    return 0;
}

/* The reply's head and page into out (cap bytes): its length, 0 when it doesn't fit. code 303 sends
 * the phone to the page; cookie signs it in. */
static unsigned reply(char *out, unsigned cap, int code, int cookie, const char *page) {
    char c[80] = "";
    unsigned len = strlen(page);
    if (cookie)
        snprintf(c, sizeof c, "Set-Cookie: " PH_COOKIE "%s; Path=/; HttpOnly; SameSite=Lax\r\n",
                 ph.key);
    int n = snprintf(out, cap,
                     "HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "
                     "%u\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nConnection:"
                     " close\r\n%s%s\r\n",
                     code == 200   ? "200 OK"
                     : code == 303 ? "303 See Other"
                                   : "404 Not Found",
                     len, c, code == 303 ? "Location: /\r\n" : "");
    if (n < 0 || (unsigned)n + len >= cap) return 0;
    memcpy(out + n, page, len);
    return (unsigned)n + len;
}

/* The reply to a request (head: its lines, NUL-ended; body: its form, NUL-ended) into out (cap
 * bytes): its length. The QR code's /?k=key and the PIN sign a phone in (a cookie of the key); a
 * signed-in phone adds and removes (each sent back to the page, which says how it went). */
static unsigned answer(const char *head, const char *body, char *out, unsigned cap) {
    char path[64], query[96], got[64], cookie[96], *c;
    int post = !strncmp(head, "POST ", 5), i = 0, code = 200, sign = 0;
    const char *p = head + (post ? 5 : !strncmp(head, "GET ", 4) ? 4 : 0);
    text_t t = { calloc(1, PH_PAGE), 0, PH_PAGE };
    if (!t.s) return 0;
    while (*p && *p != ' ' && *p != '?' && i + 1 < (int)sizeof path) path[i++] = *p++;
    path[i] = 0, i = 0;
    if (*p == '?')
        while (*++p && *p != ' ' && i + 1 < (int)sizeof query) query[i++] = *p;
    query[i] = 0;
    header(head, "Cookie", cookie, sizeof cookie);
    c = strstr(cookie, PH_COOKIE);
    char end = c ? c[strlen(PH_COOKIE) + 16] : 1; /* what follows the key: none, or the next */
    int in = c && !strncmp(c + strlen(PH_COOKIE), ph.key, 16) && (!end || end == ';' || end == ' ');
    if (head == p || (!post && strncmp(head, "GET ", 4))) {
        code = 404;
    } else if (!post && !strcmp(path, "/") && form_field(query, "k", got, sizeof got) &&
               !strcmp(got, ph.key)) {
        code = 303, sign = 1; /* the QR code's address, the key out of the address bar */
    } else if (!in && post && !strcmp(path, "/pin")) {
        char digits[8];
        int d = 0;
        form_field(body, "pin", got, sizeof got);
        for (char *g = got; *g && d < 7; ++g)
            if (*g != ' ') digits[d++] = *g;
        digits[d] = 0;
        if (ph.tries < PH_TRIES && !strcmp(digits, ph.pin))
            code = 303, sign = 1;
        else {
            snprintf(ph.said, sizeof ph.said, "%s",
                     ++ph.tries < PH_TRIES ? "That isn't the PIN the Q2 shows"
                                           : "Too many wrong PINs. On the Q2, press Return, "
                                             "then Add an App again for a new one");
            page_pin(&t);
        }
    } else if (!in && !strcmp(path, "/")) {
        page_pin(&t);
    } else if (!in) {
        code = 303;
    } else if (post && !strcmp(path, "/add")) {
        add(body), code = 303;
    } else if (post && !strcmp(path, "/remove")) {
        drop(body), code = 303;
    } else if (!strcmp(path, "/")) {
        page_main(&t);
    } else
        code = 404;
    unsigned n = reply(out, cap, code, sign, t.s);
    free(t.s);
    return n;
}

/* fd ready to read (or write) within 200 ms: 1, 0 when not yet, -1 when it fails. */
static int ready(int fd, int write) {
    unsigned set[32] = { 0 }; /* fd_set */
    int tv[2] = { 0, 200000 };
    set[fd / 32] = 1u << fd % 32;
    int k = select(fd + 1, write ? 0 : set, write ? set : 0, 0, tv);
    return k > 0 ? 1 : k;
}

/* The request's Content-Length (head: its lines). */
static unsigned length(const char *head) {
    char v[16];
    return header(head, "Content-Length", v, sizeof v) ? (unsigned)atoi(v) : 0;
}

/* One connection: its request read whole within PH_WAIT_MS (else dropped), answered. */
static void talk(int c, char *buf, char *out) {
    unsigned n = 0, head = 0, want = 0, at = (unsigned)now_ms();
    fcntl(c, 4, 0x80); /* F_SETFL, O_NONBLOCK (MIPS) */
    while (!ph.stop && (unsigned)now_ms() - at < PH_WAIT_MS && (!head || n < want)) {
        int k = ready(c, 0);
        if (k < 0) return;
        if (!k) continue;
        if ((k = recv(c, buf + n, PH_REQ - n, 0)) <= 0) return;
        n += (unsigned)k, buf[n] = 0;
        char *e = head ? 0 : strstr(buf, "\r\n\r\n");
        if (e) head = (unsigned)(e - buf) + 4, buf[head - 2] = 0, want = head + length(buf);
        if (want > PH_REQ || (!head && n == PH_REQ)) return;
    }
    if (!head || n < want) return;
    buf[want] = 0;
    ph.used = (unsigned)now_ms();
    unsigned len = answer(buf, buf + head, out, PH_OUT);
    for (unsigned sent = 0; sent < len && (unsigned)now_ms() - at < 2 * PH_WAIT_MS;) {
        int k = ready(c, 1);
        if (k < 0) return;
        if (k && (k = send(c, out + sent, len - sent, 0x4000)) <= 0) return; /* MSG_NOSIGNAL */
        sent += (unsigned)k;
    }
}

/* The server until the QR page closes, the address looked up every 2 s; then PH_STATE gone. Each
 * connection waits (PH_IDLE_MS at most, PH_CLIENTS of them, the oldest closed for a new one) until
 * its request starts, then has the thread to itself: a browser's spare connection holds none up. */
static void *serve(void *unused) {
    char *buf = calloc(1, PH_REQ + 1), *out = calloc(1, PH_OUT);
    unsigned looked = (unsigned)now_ms(), since[PH_CLIENTS];
    int cl[PH_CLIENTS], n = 0;
    while (buf && out && !ph.stop) {
        unsigned set[32] = { 0 }, now; /* fd_set */
        int tv[2] = { 1, 0 }, top = ph.fd, k;
        if ((unsigned)now_ms() - looked >= 2000) looked = (unsigned)now_ms(), ph_state(0);
        set[ph.fd / 32] |= 1u << ph.fd % 32;
        for (int i = 0; i < n; ++i)
            set[cl[i] / 32] |= 1u << cl[i] % 32, top = cl[i] > top ? cl[i] : top;
        if ((k = select(top + 1, set, 0, 0, tv)) < 0) usleep(100000); /* a signal, or worse */
        now = (unsigned)now_ms();
        for (int i = 0; i < n;) /* a request under way, or nothing too long */
            if ((k > 0 && set[cl[i] / 32] >> cl[i] % 32 & 1) || now - since[i] >= PH_IDLE_MS) {
                if (k > 0 && set[cl[i] / 32] >> cl[i] % 32 & 1) talk(cl[i], buf, out);
                close(cl[i]);
                memmove(cl + i, cl + i + 1, (n - i - 1) * sizeof *cl);
                memmove(since + i, since + i + 1, (n - i - 1) * sizeof *since), --n;
            } else
                ++i;
        int c = k > 0 && set[ph.fd / 32] >> ph.fd % 32 & 1 ? accept(ph.fd, 0, 0) : -1;
        if (c >= 1024) close(c), c = -1; /* past an fd_set */
        if (c >= 0 && n == PH_CLIENTS) {
            close(cl[0]);
            memmove(cl, cl + 1, (n - 1) * sizeof *cl);
            memmove(since, since + 1, (n - 1) * sizeof *since), --n;
        }
        if (c >= 0) cl[n] = c, since[n++] = (unsigned)now_ms();
    }
    for (int i = 0; i < n; ++i) close(cl[i]);
    close(ph.fd);
    unlink(PH_STATE);
    free(buf), free(out);
    ph.running = 0;
    return 0;
}

/* The QR page closed (addon.c): the server stops within a second, a GitHub check under way too. */
static void phone_closed(void) { ph.stop = 1, ph.xfer[0] = 1; }

/* Every 500 ms from the press until the server ended and the App Store took what it changed: the
 * Q2 kept from standby while the phone is used; the App Store's list read again once the QR page
 * closed (so its rows change under no one), when no job holds it. */
static int tick(const void *unused) {
    int seq = ph.seq;
    if (ph.running && (unsigned)now_ms() - ph.used < PH_AWAKE_MS) reset_poweroptions_timer(1, 1, 0);
    if (ph.stop && seq != ph.seen && shelf_reload()) ph.seen = seq;
    if (ph.running || ph.seq != ph.seen) return 8; /* RET_REPEAT */
    ph.timer = 0;
    return 0;
}

/* Add an App's press (netupdate.c, with Wi-Fi): the server started on the first free port from
 * PH_PORT, a new key and PIN, and the QR page opened on PH_STATE. 0 when it can't (or the last one
 * is still ending, which takes a second). */
int phone_open(void) {
    struct ph_addr a = { 2, 0, { 0 }, { 0 } }; /* AF_INET, any address */
    int fd, one = 1, port = 0;
    if (ph.running) {
        toast("Wait a moment");
        return 0;
    }
    if ((fd = socket(2, 2, 0)) >= 0 && fd < 1024) {  /* AF_INET, SOCK_STREAM (MIPS numbering) */
        setsockopt(fd, 0xffff, 4, &one, sizeof one); /* SOL_SOCKET, SO_REUSEADDR (MIPS) */
        for (int p = PH_PORT; !port && p < PH_PORT + PH_PORTS; ++p) {
            a.port = (unsigned short)(p >> 8 | (p & 255) << 8); /* network order */
            if (!bind(fd, &a, sizeof a)) port = p;
        }
    }
    if (!port || listen(fd, 4)) {
        if (fd >= 0) close(fd);
        toast("Can't open the page for the phone");
        return 0;
    }
    ph.fd = fd, ph.port = port, ph.stop = ph.tries = ph.xfer[0] = ph.xfer[1] = 0;
    ph.used = (unsigned)now_ms(), *ph.said = *ph.ip = 0;
    secret(ph.key, 16, "0123456789abcdef", 16);
    secret(ph.pin, 6, "0123456789", 10);
    snprintf(ph.note, sizeof ph.note, "Paste an app's GitHub link there");
    ph_state(1);
    ph.running = 1;
    if (pthread_create(&ph.thread, 0, serve, 0)) {
        ph.running = 0;
        close(fd);
        unlink(PH_STATE);
        toast("Can't open the page for the phone");
        return 0;
    }
    pthread_detach(ph.thread);
    if (!addon_qr_page(PH_STATE, "Add an App", phone_closed)) phone_closed();
    if (!ph.timer) ph.timer = timer_add(tick, 0, 500);
    return 1;
}

int phone_running(void) { return ph.running; }
