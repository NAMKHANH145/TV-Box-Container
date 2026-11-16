/* update.c — package signature verification (P0.6).
 *
 * Packages are signed in minisign format (Ed25519).  Instead of vendoring
 * libsodium we decode the minisign armored key + signature ourselves and let
 * `openssl pkeyutl` do the curve maths: the key becomes a DER SPKI blob and
 * the signature stays a raw 64-byte Ed25519 signature.
 *
 *   pubkey : "untrusted comment: ...\n" + b64(alg[2] || keyid[8] || pk[32])
 *   .sig   : "untrusted comment: ...\n" + b64(alg[2] || keyid[8] || sig[64])
 *            "trusted comment: ...\n" + b64(global_sig[64])
 *
 * We verify the file signature (the part that covers the package bytes).
 */
#include "common.h"

static int b64v(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int b64_decode(const char *in, unsigned char *out, size_t outn) {
    unsigned acc = 0;
    int bits = 0;
    size_t o = 0;
    for (const char *p = in; *p; p++) {
        if (*p == '=' || *p == '\n' || *p == '\r') break;
        int v = b64v((unsigned char)*p);
        if (v < 0) return -1;
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= outn) return -1;
            out[o++] = (unsigned char)((acc >> bits) & 0xff);
        }
    }
    return (int)o;
}

static void b64_encode(const unsigned char *in, size_t n, char *out) {
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, o = 0;
    while (i + 3 <= n) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8) | in[i + 2];
        out[o++] = tab[(v >> 18) & 63]; out[o++] = tab[(v >> 12) & 63];
        out[o++] = tab[(v >> 6) & 63];  out[o++] = tab[v & 63];
        i += 3;
    }
    if (n - i == 1) {
        unsigned v = (unsigned)in[i] << 16;
        out[o++] = tab[(v >> 18) & 63]; out[o++] = tab[(v >> 12) & 63];
        out[o++] = '='; out[o++] = '=';
    } else if (n - i == 2) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8);
        out[o++] = tab[(v >> 18) & 63]; out[o++] = tab[(v >> 12) & 63];
        out[o++] = tab[(v >> 6) & 63];  out[o++] = '=';
    }
    out[o] = '\0';
}

/* First base64 line that is not a comment; returns decoded length or -1. */
static int read_armor(const char *path, unsigned char *out, size_t outn) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[BUF_SIZE_XLARGE];
    int n = -1;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "untrusted comment:", 18) == 0) continue;
        if (strncmp(line, "trusted comment:", 16) == 0) break;
        n = b64_decode(line, out, outn);
        if (n > 0) break;
    }
    fclose(f);
    return n;
}

int verify_signature(const char *pkg, const char *keypath) {
    unsigned char pub[BUF_SIZE_MEDIUM], sig[BUF_SIZE_MEDIUM];
    int np = read_armor(keypath, pub, sizeof pub);
    if (np != 42) {
        fprintf(stderr, "minicontainer: verify: '%s' is not a minisign public key\n", keypath);
        return -1;
    }
    char sigpath[PATH_MAX + 8];
    if (snprintf(sigpath, sizeof sigpath, "%s.sig", pkg) >= (int)sizeof sigpath) return -1;
    int ns = read_armor(sigpath, sig, sizeof sig);
    if (ns != 74) {
        fprintf(stderr, "minicontainer: verify: missing or malformed %s\n", sigpath);
        return -1;
    }
    if (memcmp(pub + 2, sig + 2, 8) != 0) {
        fprintf(stderr, "minicontainer: verify: signature was made with a different key\n");
        return -1;
    }

    /* Ed25519 SubjectPublicKeyInfo: 302a300506032b6570032100 || pk */
    static const unsigned char der[] = {
        0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00
    };
    unsigned char spki[sizeof der + 32];
    memcpy(spki, der, sizeof der);
    memcpy(spki + sizeof der, pub + 10, 32);
    char b64[BUF_SIZE_MEDIUM];
    b64_encode(spki, sizeof spki, b64);

    char pemfile[] = "/tmp/mc-key-XXXXXX", rawfile[] = "/tmp/mc-sig-XXXXXX";
    int kfd = mkstemp(pemfile);
    int sfd = mkstemp(rawfile);
    if (kfd < 0 || sfd < 0) {
        fprintf(stderr, "minicontainer: verify: cannot create temp file: %s\n", strerror(errno));
        if (kfd >= 0) { close(kfd); unlink(pemfile); }
        if (sfd >= 0) { close(sfd); unlink(rawfile); }
        return -1;
    }
    char pem[BUF_SIZE_MEDIUM];
    int pl = snprintf(pem, sizeof pem, "-----BEGIN PUBLIC KEY-----\n%s\n-----END PUBLIC KEY-----\n", b64);
    int ok = 0;
    if (pl > 0 && write(kfd, pem, (size_t)pl) == pl &&
        write(sfd, sig + 10, 64) == 64) ok = 1;
    close(kfd);
    close(sfd);
    if (!ok) {
        fprintf(stderr, "minicontainer: verify: cannot write temp file\n");
        unlink(pemfile); unlink(rawfile);
        return -1;
    }

    char *const av[] = { "openssl", "pkeyutl", "-verify", "-pubin", "-inkey", pemfile,
                         "-rawin", "-in", (char *)pkg, "-sigfile", rawfile, NULL };
    int rc = run_wait(av);
    unlink(pemfile);
    unlink(rawfile);
    if (rc != 0) {
        fprintf(stderr, "minicontainer: verify: signature KHONG hop le cho %s\n", pkg);
        return -1;
    }
    printf("verify: chu ky hop le (%s)\n", keypath);
    return 0;
}
