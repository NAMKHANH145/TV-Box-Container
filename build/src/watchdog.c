/* watchdog.c — P4.3: feed /dev/watchdog while a container is alive.
 *
 * The hardware watchdog is the only defence against a kernel/userspace hang
 * that leaves the box unreachable.  Feed every timeout/2; stop feeding when
 * the supervised PID exits or on SIGTERM/SIGINT (magic close 'V' disarms the
 * driver so an ordinary `stop` does not reboot the box).
 *
 *   minicontainer watchdog [--pid=PID] [--timeout=30] [--device=/dev/watchdog]
 *
 * With no --pid it feeds until killed.  --device is for tests (a regular file
 * works; the ioctl timeout query is best-effort). */
#include "common.h"
#include <sys/ioctl.h>
#ifdef __linux__
#include <linux/watchdog.h>
#endif
#ifndef WDIOC_SETTIMEOUT
#define WDIOC_SETTIMEOUT _IOWR('W', 6, int)
#endif
#ifndef WDIOC_GETTIMEOUT
#define WDIOC_GETTIMEOUT _IOR('W', 7, int)
#endif

static volatile sig_atomic_t wd_stop;

static void wd_signal(int sig) {
    (void)sig;
    wd_stop = 1;
}

static int pid_alive(pid_t pid) {
    if (pid <= 0) return 1;
    if (kill(pid, 0) == 0) return 1;
    return errno == EPERM;
}

int do_watchdog(int argc, char **argv) {
    const char *dev = "/dev/watchdog";
    int timeout = 30;
    pid_t pid = 0;
    int custom_dev = 0;

    for (int i = 2; i < argc; i++) {
        if (strncmp(argv[i], "--device=", 9) == 0) { dev = argv[i] + 9; custom_dev = 1; }
        else if (strncmp(argv[i], "--timeout=", 10) == 0) timeout = atoi(argv[i] + 10);
        else if (strncmp(argv[i], "--pid=", 6) == 0) pid = (pid_t)atoi(argv[i] + 6);
        else {
            fprintf(stderr, "Usage: minicontainer watchdog [--pid=PID] [--timeout=SEC] [--device=PATH]\n");
            return 1;
        }
    }
    if (timeout < 1) timeout = 1;

    /* A custom --device is a test file; never create a missing real watchdog. */
    int fd = open(dev, O_WRONLY | O_CLOEXEC | (custom_dev ? O_CREAT : 0), 0600);
    if (fd < 0) {
        fprintf(stderr, "watchdog: %s: %s\n", dev, strerror(errno));
        return 1;
    }
    int t = timeout;
    int got = -1;
    if (ioctl(fd, WDIOC_SETTIMEOUT, &t) == 0) {
        int actual = 0;
        if (ioctl(fd, WDIOC_GETTIMEOUT, &actual) == 0 && actual > 0) got = actual;
    }
    if (got < 0) got = timeout;
    fprintf(stderr, "watchdog: %s, timeout %ds%s\n", dev, got,
            pid > 0 ? "" : " (until signal)");

    struct sigaction sa = { .sa_handler = wd_signal };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    int interval = got > 1 ? got / 2 : 1;
    int rc = 0;
    while (!wd_stop) {
        if (pid > 0 && !pid_alive(pid)) break;
        ssize_t w = write(fd, "\0", 1);
        if (w != 1) {
            fprintf(stderr, "watchdog: feed failed: %s\n", strerror(errno));
            rc = 1;
            break;
        }
        sleep((unsigned)interval);
    }

    /* Magic close disarms; do not reboot because the supervisor asked to stop. */
    if (write(fd, "V", 1) != 1) { /* best effort */ }
    close(fd);
    return rc;
}
