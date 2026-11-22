/* appinit.c — `init`: tao du an container tu lenh, khong viet Dockerfile tay. */
#include "common.h"

int do_init(int argc, char **argv) {
    const char *dir = NULL, *base = "python:3.12-alpine";
    char pkgs[BUF_SIZE_MEDIUM] = "";
    size_t plen = 0;

    for (int i = 2; i < argc; i++) {
        const char *p = NULL;
        if (strncmp(argv[i], "--pip=", 6) == 0) p = argv[i] + 6;
        else if (strcmp(argv[i], "--pip") == 0 && i + 1 < argc) p = argv[++i];
        else if (strncmp(argv[i], "--base=", 7) == 0) base = argv[i] + 7;
        else if (argv[i][0] != '-' && !dir) dir = argv[i];
        else {
            fprintf(stderr, "Usage: %s init <dir> [--pip=PKG]... [--base=IMAGE]\n", argv[0]);
            return 1;
        }
        if (p && *p) {
            size_t l = strlen(p);
            if (plen + l + 2 >= sizeof pkgs) {
                fprintf(stderr, "init: too many packages\n");
                return 1;
            }
            if (plen) pkgs[plen++] = ' ';
            memcpy(pkgs + plen, p, l + 1);
            plen += l;
        }
    }
    if (!dir) {
        fprintf(stderr, "Usage: %s init <dir> [--pip=PKG]... [--base=IMAGE]\n", argv[0]);
        return 1;
    }
    if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "init: cannot create %s: %s\n", dir, strerror(errno));
        return 1;
    }

    char path[PATH_MAX], buf[2048];
    fmt_path(path, sizeof path, "%s/Dockerfile", dir);
    int n = snprintf(buf, sizeof buf, "FROM %s\n", base);
    if (pkgs[0]) n += snprintf(buf + n, sizeof buf - n,
                              "RUN pip install --no-cache-dir %s\n", pkgs);
    n += snprintf(buf + n, sizeof buf - n,
                  "COPY app.py /app/app.py\n"
                  "CMD [\"python\", \"/app/app.py\"]\n");
    write_file(path, buf);

    fmt_path(path, sizeof path, "%s/app.py", dir);
    write_file(path,
        "#!/usr/bin/env python3\n"
        "import json, os, platform\n"
        "\n"
        "print(json.dumps({\n"
        "    \"arch\": platform.machine(),\n"
        "    \"kernel\": open(\"/proc/sys/kernel/osrelease\").read().strip(),\n"
        "    \"pid\": os.getpid(),\n"
        "    \"message\": \"hello from minicontainer\",\n"
        "}, sort_keys=True))\n");

    fmt_path(path, sizeof path, "%s/README.md", dir);
    write_file(path,
        "# Container app (sinh boi `minicontainer init`)\n\n"
        "```bash\n"
        "minicontainer pull python:3.12-alpine\n"
        "minicontainer build -t myapp:1 .\n"
        "minicontainer run --image=myapp-1 python /app/app.py\n"
        "minicontainer export myapp-1 -o myapp.tar.gz\n"
        "```\n"
        "Build tren may nao thi pip se chon wheel cua may do.\n");

    printf("init: %s duoc tao (Dockerfile + app.py + README.md)\n", dir);
    printf("  base  : %s\n", base);
    if (pkgs[0]) printf("  pip   : %s\n", pkgs);
    printf("  tiep  : minicontainer pull %s && cd %s && minicontainer build -t myapp:1 .\n", base, dir);
    (void)n;
    return 0;
}
