// REDIS LINK: https://github.com/redis/redis/blob/90b9f08e5d1657e7bfffe43f31f6663bf469ee75/src/bitops.c#L40-L92
/* Count number of bits set in the binary array pointed by 's' and long
 * 'count' bytes. The implementation of this function is required to
 * work with an input string length up to 512 MB or more (server.proto_max_bulk_len) */
#include <stdint.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
static size_t redisPopcount(void *s, long count) {
    size_t bits = 0;
    unsigned char *p = s;
    uint32_t *p4;
    static const unsigned char bitsinbyte[256] = {0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8};

    /* Count initial bytes not aligned to 32 bit. */
    while((unsigned long)p & 3 && count) {
        bits += bitsinbyte[*p++];
        count--;
    }

    /* Count bits 28 bytes at a time */
    p4 = (uint32_t*)p;
    while(count>=28) {
        uint32_t aux1, aux2, aux3, aux4, aux5, aux6, aux7;

        aux1 = *p4++;
        aux2 = *p4++;
        aux3 = *p4++;
        aux4 = *p4++;
        aux5 = *p4++;
        aux6 = *p4++;
        aux7 = *p4++;
        count -= 28;

        aux1 = aux1 - ((aux1 >> 1) & 0x55555555);
        aux1 = (aux1 & 0x33333333) + ((aux1 >> 2) & 0x33333333);
        aux2 = aux2 - ((aux2 >> 1) & 0x55555555);
        aux2 = (aux2 & 0x33333333) + ((aux2 >> 2) & 0x33333333);
        aux3 = aux3 - ((aux3 >> 1) & 0x55555555);
        aux3 = (aux3 & 0x33333333) + ((aux3 >> 2) & 0x33333333);
        aux4 = aux4 - ((aux4 >> 1) & 0x55555555);
        aux4 = (aux4 & 0x33333333) + ((aux4 >> 2) & 0x33333333);
        aux5 = aux5 - ((aux5 >> 1) & 0x55555555);
        aux5 = (aux5 & 0x33333333) + ((aux5 >> 2) & 0x33333333);
        aux6 = aux6 - ((aux6 >> 1) & 0x55555555);
        aux6 = (aux6 & 0x33333333) + ((aux6 >> 2) & 0x33333333);
        aux7 = aux7 - ((aux7 >> 1) & 0x55555555);
        aux7 = (aux7 & 0x33333333) + ((aux7 >> 2) & 0x33333333);
        bits += ((((aux1 + (aux1 >> 4)) & 0x0F0F0F0F) +
                    ((aux2 + (aux2 >> 4)) & 0x0F0F0F0F) +
                    ((aux3 + (aux3 >> 4)) & 0x0F0F0F0F) +
                    ((aux4 + (aux4 >> 4)) & 0x0F0F0F0F) +
                    ((aux5 + (aux5 >> 4)) & 0x0F0F0F0F) +
                    ((aux6 + (aux6 >> 4)) & 0x0F0F0F0F) +
                    ((aux7 + (aux7 >> 4)) & 0x0F0F0F0F))* 0x01010101) >> 24;
    }
    /* Count the remaining bytes. */
    p = (unsigned char*)p4;
    while(count--) bits += bitsinbyte[*p++];
    return bits;
}
#pragma GCC diagnostic pop

// // REDIS LINK: https://github.com/redis/redis/blob/90b9f08e5d1657e7bfffe43f31f6663bf469ee75/src/bitops.c#L94-L186
#include <limits.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
/* Return the position of the first bit set to one (if 'bit' is 1) or
 * zero (if 'bit' is 0) in the bitmap starting at 's' and long 'count' bytes.
 *
 * The function is guaranteed to return a value >= 0 if 'bit' is 0 since if
 * no zero bit is found, it returns count*8 assuming the string is zero
 * padded on the right. However if 'bit' is 1 it is possible that there is
 * not a single set bit in the bitmap. In this special case -1 is returned. */
static long redisBitpos(void *s, unsigned long count, int bit) {
    unsigned long *l;
    unsigned char *c;
    unsigned long skipval, word = 0, one;
    long pos = 0; /* Position of bit, to return to the caller. */
    unsigned long j;
    int found;

    /* Process whole words first, seeking for first word that is not
     * all ones or all zeros respectively if we are looking for zeros
     * or ones. This is much faster with large strings having contiguous
     * blocks of 1 or 0 bits compared to the vanilla bit per bit processing.
     *
     * Note that if we start from an address that is not aligned
     * to sizeof(unsigned long) we consume it byte by byte until it is
     * aligned. */

    /* Skip initial bits not aligned to sizeof(unsigned long) byte by byte. */
    skipval = bit ? 0 : UCHAR_MAX;
    c = (unsigned char*) s;
    found = 0;
    while((unsigned long)c & (sizeof(*l)-1) && count) {
        if (*c != skipval) {
            found = 1;
            break;
        }
        c++;
        count--;
        pos += 8;
    }

    /* Skip bits with full word step. */
    l = (unsigned long*) c;
    if (!found) {
        skipval = bit ? 0 : ULONG_MAX;
        while (count >= sizeof(*l)) {
            if (*l != skipval) break;
            l++;
            count -= sizeof(*l);
            pos += sizeof(*l)*8;
        }
    }

    /* Load bytes into "word" considering the first byte as the most significant
     * (we basically consider it as written in big endian, since we consider the
     * string as a set of bits from left to right, with the first bit at position
     * zero.
     *
     * Note that the loading is designed to work even when the bytes left
     * (count) are less than a full word. We pad it with zero on the right. */
    c = (unsigned char*)l;
    for (j = 0; j < sizeof(*l); j++) {
        word <<= 8;
        if (count) {
            word |= *c;
            c++;
            count--;
        }
    }

    /* Special case:
     * If bits in the string are all zero and we are looking for one,
     * return -1 to signal that there is not a single "1" in the whole
     * string. This can't happen when we are looking for "0" as we assume
     * that the right of the string is zero padded. */
    if (bit == 1 && word == 0) return -1;

    /* Last word left, scan bit by bit. The first thing we need is to
     * have a single "1" set in the most significant position in an
     * unsigned long. We don't know the size of the long so we use a
     * simple trick. */
    one = ULONG_MAX; /* All bits set to 1.*/
    one >>= 1;       /* All bits set to 1 but the MSB. */
    one = ~one;      /* All bits set to 0 but the MSB. */

    while(one) {
        if (((one & word) != 0) == bit) return pos;
        pos++;
        one >>= 1;
    }

    /* If we reached this point, there is a bug in the algorithm, since
     * the case of no match is handled as a special case before. */
    printf("End of redisBitpos() reached.\n");
    return 0; /* Just to avoid warnings. */
}
#pragma GCC diagnostic pop
