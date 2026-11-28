#include <fcntl.h>
#include <unistd.h>
#include "kspkan.h"

/* kind is "sha1" or "sha256"; hex gets 40 or 64 chars plus NUL */
int hash_file(const char *path, const char *kind, char *hex)
{
    struct sha256 a;
    struct sha1 b;
    unsigned char buf[8192];
    unsigned char dig[32];
    long n;
    int fd, is256;

    is256 = (kind[3] == '2');
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    sha256_init(&a);
    sha1_init(&b);
    n = (long)read(fd, buf, sizeof(buf));
    while (n > 0) {
        if (is256) {
            sha256_add(&a, buf, (int)n);
        } else {
            sha1_add(&b, buf, (int)n);
        }
        n = (long)read(fd, buf, sizeof(buf));
    }
    close(fd);
    if (n < 0) {
        return -1;
    }
    if (is256) {
        sha256_done(&a, dig);
        sha256_hex(dig, hex);
    } else {
        sha1_done(&b, dig);
        sha1_hex(dig, hex);
    }
    return 0;
}
