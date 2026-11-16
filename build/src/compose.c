/* compose.c — P4.5: compose-lite for a handful of services on one box.
 *
 * Deliberately not a full YAML engine: a strict subset of docker-compose.yml
 * that covers what a TV Box deployment needs.
 *
 *   services:
 *     web:
 *       image: nginx:alpine
 *       ports: ["8080:80"]
 *       environment: ["TZ=Asia/Ho_Chi_Minh"]
 *       command: ["/bin/sh", "-c", "exec nginx -g 'daemon off;'"]
 *       network: bridge        # bridge|host|isolated, default bridge
 *       restart: always
 *       memory: 64M
 *       cpus: 20
 *       volumes: ["/srv/www:/usr/share/nginx/html:ro"]
 *
 * `up` starts every service (detached), rolling back the ones already started
 * if a later service fails. `down` stops and removes them. Service names are
 * container names and therefore bridge hostnames. */
#include "common.h"

#define CS_MAX 16
#define CS_LIST 8
#define CS_ENV 8
#define CS_WORDS 32

struct csvc {
    char name[BUF_SIZE_SMALL];
    char image[BUF_SIZE_MEDIUM];
    char rootfs[PATH_MAX];
    char net[BUF_SIZE_SMALL];
    char restart[BUF_SIZE_SMALL];
    char memory[BUF_SIZE_SMALL];
    int cpus;
    char cmdstore[BUF_SIZE_XLARGE];
    char *argv[CS_WORDS];
    int argc;
    char ports[CS_LIST][BUF_SIZE_SMALL];
    int nports;
    char env[CS_ENV][BUF_SIZE_MEDIUM];
    int nenv;
    char mounts[CS_LIST][PATH_MAX + 8];
    int nmounts;
};

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static void unquote(char *s) {
    size_t n = strlen(s);
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') || (s[0] == '\'' && s[n - 1] == '\''))) {
        memmove(s, s + 1, n - 2);
        s[n - 2] = 0;
    }
}

static void strip_comment(char *s) {
    char q = 0;
    for (char *p = s; *p; p++) {
        if (q) {
            if (*p == q) q = 0;
        } else if (*p == '"' || *p == '\'') {
            q = *p;
        } else if (*p == '#' && (p == s || p[-1] == ' ' || p[-1] == '\t')) {
            *p = 0;
            return;
        }
    }
}

static int indent_of(const char *s) {
    int n = 0;
    while (s[n] == ' ') n++;
    return n;
}

/* Copy one item from "[a, 'b', c]" or a bare scalar. */
static int next_item(char **pp, char *out, size_t n) {
    char *p = *pp;
    while (*p == ' ' || *p == '\t' || *p == ',' || *p == '[') p++;
    if (!*p || *p == ']') { *pp = p; return 0; }
    size_t o = 0;
    char q = 0;
    if (*p == '"' || *p == '\'') q = *p++;
    while (*p) {
        if (q) {
            if (*p == q) { p++; break; }
            if (*p == '\\' && p[1]) p++;
        } else if (*p == ',' || *p == ']') {
            break;
        }
        if (o + 1 < n) out[o++] = *p;
        p++;
    }
    out[o] = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (!q && *p == ',') p++;
    *pp = p;
    return 1;
}

static void add_item(char out[][BUF_SIZE_SMALL], int *n, int max, const char *v) {
    if (*n >= max) return;
    if (fmt_path(out[*n], BUF_SIZE_SMALL, "%s", v) == 0) (*n)++;
}

static void add_env(struct csvc *s, const char *v) {
    if (s->nenv >= CS_ENV) return;
    if (fmt_path(s->env[s->nenv], BUF_SIZE_MEDIUM, "%s", v) == 0) s->nenv++;
}

static void add_mount(struct csvc *s, const char *v) {
    if (s->nmounts >= CS_LIST) return;
    if (strncmp(v, "bind:", 5) == 0)
        snprintf(s->mounts[s->nmounts++], PATH_MAX + 8, "%s", v);
    else
        snprintf(s->mounts[s->nmounts++], PATH_MAX + 8, "bind:%s", v);
}

/* Tokenize a command: shell words for a string, or comma items for [a, b]. */
static void parse_command(struct csvc *s, const char *val) {
    int json = val[0] == '[';
    char *p = (char *)val;
    s->argc = 0;
    size_t o = 0;
    while (*p && s->argc < CS_WORDS - 1) {
        while (*p == ' ' || *p == '\t' || (json && (*p == ',' || *p == '[' || *p == ']'))) p++;
        if (!*p || (json && *p == ']')) break;
        char q = 0;
        if (*p == '"' || *p == '\'') { q = *p++; }
        char *start = &s->cmdstore[o];
        while (*p) {
            if (q) {
                if (*p == q) { p++; break; }
                if (*p == '\\' && p[1]) p++;
            } else if (json) {
                if (*p == ',' || *p == ']') break;
            } else if (*p == ' ' || *p == '\t') {
                break;
            }
            if (o + 1 < sizeof s->cmdstore) s->cmdstore[o++] = *p;
            p++;
        }
        s->cmdstore[o++] = 0;
        s->argv[s->argc++] = start;
        while (*p == ' ' || *p == '\t') p++;
        if (json && *p == ',') p++;
    }
    s->argv[s->argc] = NULL;
}

static int parse_file(const char *path, struct csvc *svc, int *nsvc) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "compose: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    char line[BUF_SIZE_HUGE];
    int in_services = 0, listmode = 0, line_no = 0;
    struct csvc *cur = NULL;
    *nsvc = 0;

    while (fgets(line, sizeof line, f)) {
        line_no++;
        int ind = indent_of(line);
        char *t = trim(line);
        strip_comment(t);
        t = trim(t);
        if (!*t) continue;
        if (ind == 0) {
            if (strcmp(t, "services:") == 0) {
                in_services = 1;
                cur = NULL;
            } else {
                in_services = 0;
            }
            continue;
        }
        if (!in_services) continue;
        if (ind <= 2) {
            if (t[strlen(t) - 1] != ':') continue;
            t[strlen(t) - 1] = 0;
            char *nm = trim(t);
            if (!valid_name(nm)) {
                fprintf(stderr, "compose: %s:%d: invalid service name '%s'\n", path, line_no, nm);
                fclose(f);
                return -1;
            }
            if (*nsvc >= CS_MAX) {
                fprintf(stderr, "compose: too many services (max %d)\n", CS_MAX);
                fclose(f);
                return -1;
            }
            cur = &svc[(*nsvc)++];
            memset(cur, 0, sizeof *cur);
            snprintf(cur->name, sizeof cur->name, "%s", nm);
            listmode = 0;
            continue;
        }
        if (!cur) continue;
        if (t[0] == '-' && listmode) {
            char v[BUF_SIZE_LARGE];
            snprintf(v, sizeof v, "%s", trim(t + 1));
            unquote(v);
            if (listmode == 1) add_item(cur->ports, &cur->nports, CS_LIST, v);
            else if (listmode == 2) add_env(cur, v);
            else if (listmode == 3) add_mount(cur, v);
            continue;
        }

        char *colon = strchr(t, ':');
        if (!colon) continue;
        *colon = 0;
        char *key = trim(t);
        char *val = trim(colon + 1);
        unquote(val);
        listmode = 0;
        if (strcmp(key, "image") == 0) {
            snprintf(cur->image, sizeof cur->image, "%s", val);
        } else if (strcmp(key, "rootfs") == 0) {
            snprintf(cur->rootfs, sizeof cur->rootfs, "%s", val);
        } else if (strcmp(key, "network") == 0) {
            snprintf(cur->net, sizeof cur->net, "%s", val);
        } else if (strcmp(key, "restart") == 0) {
            snprintf(cur->restart, sizeof cur->restart, "%s", val);
        } else if (strcmp(key, "memory") == 0) {
            snprintf(cur->memory, sizeof cur->memory, "%s", val);
        } else if (strcmp(key, "cpus") == 0) {
            cur->cpus = atoi(val);
        } else if (strcmp(key, "command") == 0) {
            parse_command(cur, val);
        } else if (strcmp(key, "ports") == 0) {
            if (val[0] == '[') {
                char *p = val;
                char v[BUF_SIZE_SMALL];
                while (next_item(&p, v, sizeof v)) add_item(cur->ports, &cur->nports, CS_LIST, v);
            } else if (*val) {
                add_item(cur->ports, &cur->nports, CS_LIST, val);
            } else {
                listmode = 1;
            }
        } else if (strcmp(key, "environment") == 0) {
            if (val[0] == '[') {
                char *p = val;
                char v[BUF_SIZE_MEDIUM];
                while (next_item(&p, v, sizeof v)) add_env(cur, v);
            } else if (*val) {
                add_env(cur, val);
            } else {
                listmode = 2;
            }
        } else if (strcmp(key, "volumes") == 0) {
            if (val[0] == '[') {
                char *p = val;
                char v[PATH_MAX];
                while (next_item(&p, v, sizeof v)) add_mount(cur, v);
            } else if (*val) {
                add_mount(cur, val);
            } else {
                listmode = 3;
            }
        }
    }
    fclose(f);

    if (*nsvc == 0) {
        fprintf(stderr, "compose: %s has no services\n", path);
        return -1;
    }
    for (int i = 0; i < *nsvc; i++) {
        struct csvc *s = &svc[i];
        if (!s->image[0] && !s->rootfs[0]) {
            fprintf(stderr, "compose: service '%s' needs image or rootfs\n", s->name);
            return -1;
        }
        if (!s->net[0]) snprintf(s->net, sizeof s->net, "bridge");
        if (!s->restart[0]) snprintf(s->restart, sizeof s->restart, "no");
    }
    return 0;
}

static int state_exists(const char *name) {
    char p[PATH_MAX];
    if (fmt_path(p, sizeof p, "%s/%s", STATE_BASE, name) < 0) return 0;
    struct stat st;
    return stat(p, &st) == 0;
}

static int compose_up(const char *file) {
    struct csvc svc[CS_MAX];
    int n = 0;
    if (parse_file(file, svc, &n) < 0) return 1;

    int started = 0;
    for (int i = 0; i < n; i++) {
        struct csvc *s = &svc[i];
        if (state_exists(s->name)) {
            fprintf(stderr, "compose: container '%s' already exists (use compose down first)\n", s->name);
            goto fail;
        }
        struct cfg c;
        memset(&c, 0, sizeof c);
        c.ram = -1;
        c.cpu = -1;
        c.pids = 64;
        c.io = -1;
        c.max_disk = -1;
        c.detach = 1;
        c.name = s->name;
        c.hostname = s->name;
        c.net = s->net;
        c.restart = s->restart;
        if (s->image[0]) c.image = s->image;
        if (s->rootfs[0]) c.rootfs = s->rootfs;
        if (s->argc > 0) c.cmd = s->argv;
        if (s->nports > 0) { c.publish = (char **)s->ports; c.npublish = s->nports; }
        if (s->nenv > 0) { c.env = (char **)s->env; c.nenv = s->nenv; }
        if (s->nmounts > 0) { c.mounts = (char **)s->mounts; c.nmounts = s->nmounts; }
        if (s->memory[0]) {
            long b = parse_bytes(s->memory);
            if (b <= 0) {
                fprintf(stderr, "compose: service '%s': bad memory '%s'\n", s->name, s->memory);
                goto fail;
            }
            c.ram = b;
        }
        if (s->cpus > 0) c.cpu = s->cpus;

        printf("compose: starting %s (%s)\n", s->name,
               s->image[0] ? s->image : s->rootfs);
        validate_input(&c);
        if (do_run(&c) != 0) {
            fprintf(stderr, "compose: service '%s' failed\n", s->name);
            goto fail;
        }
        started++;
    }
    printf("compose: up (%d service%s)\n", n, n == 1 ? "" : "s");
    return 0;

fail:
    for (int i = 0; i < started; i++) {
        do_stop(svc[i].name, 3);
        do_rm(svc[i].name, 1);
    }
    return 1;
}

static int compose_down(const char *file) {
    struct csvc svc[CS_MAX];
    int n = 0;
    if (parse_file(file, svc, &n) < 0) return 1;
    for (int i = 0; i < n; i++) {
        do_stop(svc[i].name, 3);
        do_rm(svc[i].name, 1);
    }
    printf("compose: down (%d service%s)\n", n, n == 1 ? "" : "s");
    return 0;
}

int do_compose(int argc, char **argv) {
    const char *file = NULL;
    const char *cmd = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) {
            if (i + 1 < argc) file = argv[++i];
        } else if (strncmp(argv[i], "--file=", 7) == 0) {
            file = argv[i] + 7;
        } else if (!cmd) {
            cmd = argv[i];
        } else {
            fprintf(stderr, "Usage: minicontainer compose [-f FILE] <up|down|ps>\n");
            return 1;
        }
    }
    if (!cmd) {
        fprintf(stderr, "Usage: minicontainer compose [-f FILE] <up|down|ps>\n");
        return 1;
    }
    if (!file) {
        if (access("docker-compose.yml", R_OK) == 0) file = "docker-compose.yml";
        else if (access("compose.yml", R_OK) == 0) file = "compose.yml";
        else {
            fprintf(stderr, "compose: no docker-compose.yml or compose.yml (use -f)\n");
            return 1;
        }
    }
    if (strcmp(cmd, "up") == 0) return compose_up(file);
    if (strcmp(cmd, "down") == 0) return compose_down(file);
    if (strcmp(cmd, "ps") == 0) return do_ps(0);
    fprintf(stderr, "compose: unknown command '%s' (up|down|ps)\n", cmd);
    return 1;
}
