/* Thu chroot-break kinh dien: neu thoat duoc ra host thi doc /HOST_MARKER */
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
int main() {
    mkdir("/tmp/bb", 0755);
    if (chroot("/tmp/bb") < 0) { printf("chroot bi chan: %s (tot)\n", strerror(errno)); return 2; }
    for (int i = 0; i < 50; i++) chdir("..");
    if (chroot(".") < 0) { printf("re-chroot bi chan: %s (tot)\n", strerror(errno)); return 2; }
    FILE *f = fopen("/HOST_MARKER", "r");
    if (!f) { printf("KHONG thoat duoc (tot): %s\n", strerror(errno)); return 1; }
    char b[64]; fgets(b, sizeof b, f); fclose(f);
    printf("THOAT DUOC RA HOST! marker=%s", b); return 0;
}
