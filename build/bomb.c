#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main() {
    for (long i = 1; i <= 50; i++) {
        char *p = malloc(1024 * 1024);
        if (!p) { printf("malloc failed at %ld MB\n", i); return 1; }
        memset(p, 0xAA, 1024 * 1024);
        printf("allocated %ld MB\n", i);
    }
    return 0;
}
