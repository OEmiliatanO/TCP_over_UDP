#ifndef __TCP_UTILI_H__
#define __TCP_UTILI_H__

#include <random>

static std::random_device r;

uint16_t INIT_ISN()
{
    static std::default_random_engine eng(r());
    static std::uniform_int_distribution<uint16_t> uniform_dist(0, (1 << 16)-1);
    return uniform_dist(eng);
}

uint16_t tcp_checksum(void *segment, size_t len) // data, byte
{
    uint16_t *p = (uint16_t *)segment;
    uint16_t checksum = 0;
    for (size_t i = 0; i < len / 2; ++i, ++p)
        checksum += (*p) + ((checksum >> 15) & ((*p) >> 15));
    return ~checksum;
}

#endif
