/* ns.c — namespaces: clone child, chroot, user namespace map */
#include "common.h"
#include "namespace_setup.h"

/* P3.10: PID 1 reaper. Forwards the common stop signals, reaps every child
 * and exits with the app's status when the app itself exits. */
static volatile pid_t g_init_child = -1;
static void init_forward(int sig) {
    if (g_init_child > 0) kill(g_init_child, sig);
}

static void run_init(char **cmd) __attribute__((noreturn));
static void run_init(char **cmd) {
    pid_t p = fork();
    if (p < 0) die("init fork");
    if (p == 0) {
        signal(SIGCHLD, SIG_DFL);
        execvp(cmd[0], cmd);
        die("execvp");
    }
    g_init_child = p;
    struct sigaction sa = { .sa_handler = init_forward, .sa_flags = SA_RESTART };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    for (;;) {
        int st;
        pid_t w = waitpid(-1, &st, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            _exit(EXIT_SETUP_ERROR);
        }
        if (w != p) continue;              /* orphan: reaped, keep going */
        if (WIFEXITED(st)) _exit(WEXITSTATUS(st));
        _exit(EXIT_SIGNAL_BASE + WTERMSIG(st));
    }
}

int child_fn(void *arg) {
    struct child_arg *a = arg;
    struct cfg *c = a->c;
    char byte;

    if (write(a->ready_wr, "r", 1) != 1) die("ready");
    close(a->ready_wr);
    if (read(a->go_rd, &byte, 1) != 1) die("sync");

    /* --net=bridge: parent created veth and moved into our netns;
     * child configures interface via ioctl (no iproute2 needed in rootfs). */
    if (strcmp(c->net, "bridge") == 0) {
        struct bridge_cfg bc;
        if (bridge_cfg_read(a->go_rd, &bc) < 0) die("sync bridge");
        if (bridge_configure(&bc) < 0) die("bridge configure");
    }
    close(a->go_rd);

    /* New netns (isolated/bridge) starts with lo DOWN: everything connecting
     * to 127.0.0.1 will fail. Host uses host's lo, no need to touch. */
    if (strcmp(c->net, "host") != 0) {
        netns_lo_up();
        /* P3.3: allow services (nginx) to bind :80 without root caps. */
        if (netns_allow_low_ports() < 0)
            fprintf(stderr, "minicontainer: warning: cannot lower ip_unprivileged_port_start\n");
    }

    /* P1: image Env must be applied before chroot (the store lives outside). */
    if (c->image) img_apply_env(c->image);
    /* --env / process.env from OCI config: explicit flags win over image Env. */
    for (int i = 0; i < c->nenv; i++) {
        char buf[BUF_SIZE_LARGE];
        snprintf(buf, sizeof buf, "%s", c->env[i]);
        char *eq = strchr(buf, '=');
        if (!eq) {
            fprintf(stderr, "minicontainer: --env wants KEY=VALUE, got '%s'\n", c->env[i]);
            die("--env");
        }
        *eq = '\0';
        setenv(buf, eq + 1, 1);
    }

    /* P0.2: detached run — hand stdout/stderr to the logger's pipes.
     * stderr is redirected only AFTER setup so setup diagnostics (e.g. an
     * optional /dev node missing) go to the host, not into the app's logs.
     * dup2 clears O_CLOEXEC, so the copies survive execvp. */
    int logged = (a->out_w >= 0);
    if (logged) {
        dup2(a->out_w, 1);
        int nfd = open("/dev/null", O_RDONLY);
        if (nfd >= 0) { dup2(nfd, 0); if (nfd > 2) close(nfd); }
    }

    /* Full namespace setup: rootfs, dev, hosts, chroot, proc, ro, seccomp */
    namespace_child_setup(c, a->detach && !logged);

    /* Now take over stderr for the application itself (setup warnings above
     * were written to the caller's terminal / service unit). */
    if (logged) dup2(a->err_w, 2);

    /* P3.10: --init. The app's parent becomes a tiny reaper so apps that do
     * not wait() their children do not accumulate zombies. */
    if (c->init_enabled) run_init(c->cmd);

    execvp(c->cmd[0], c->cmd);
    die("execvp");
    return 1;
}

void userns_map(pid_t child) {
    char p[BUF_SIZE_SMALL], v[BUF_SIZE_SMALL];
    snprintf(p, sizeof p, "/proc/%d/setgroups", child);
    write_file(p, "deny");
    snprintf(p, sizeof p, "/proc/%d/uid_map", child);
    snprintf(v, sizeof v, UID_MAP_GID);
    write_file(p, v);
    snprintf(p, sizeof p, "/proc/%d/gid_map", child);
    write_file(p, v);
}