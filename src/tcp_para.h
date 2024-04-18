#ifndef __TCP_PARA_H__
#define __TCP_PARA_H__

constexpr unsigned int RTT = 20; // ms
constexpr size_t MSS = 1000; // byte
constexpr size_t threshold = 64000; // byte
constexpr size_t buffer_size = 512000; // byte
constexpr size_t init_rwnd = 64000;
constexpr size_t window_scale = 3; // init_rwnd << windown_scale = buffer_size
constexpr int UNDEFINED = -1;

#endif
