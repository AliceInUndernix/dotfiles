#include "kspkan.h"

unsigned int crc32_update(unsigned int crc, const unsigned char *p, long n)
{
    long i;
    int k;

    crc = ~crc;
    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xedb88320;
            } else {
                crc = crc >> 1;
            }
        }
    }
    return ~crc;
}
