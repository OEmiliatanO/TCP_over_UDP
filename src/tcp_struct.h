#ifndef __TCP_STRUCT_H__
#define __TCP_STRUCT_H__

#include <iostream>
#include <cstring>
#include <format>
#include <tcp_para.h>


namespace tcp_struct
{
    using port_t = uint16_t;
    using seq_t = uint32_t;
    struct segment
    {
        port_t src_port: 16, dst_port: 16;
        seq_t seq: 32, ack: 32;
        char header_len: 4; // words
        char padding: 6;
        bool URG: 1, ACK: 1, PSH: 1, RST: 1, SYN: 1, FIN: 1;
        uint16_t window: 16;
        uint16_t checksum: 16;
        uint16_t urg_ptr: 16;
        char data[MSS];

        void clear()
        {
            memset(this, 0, sizeof(*this));
        }
    };
}

std::ostream& operator<<(std::ostream& os, tcp_struct::segment seg)
{
    os << std::format(
            "\
            src_port = {}\n\
            dst_port = {}\n\
            seq = {}\n\
            ack = {}\n\
            header_len = {}\n\
            padding = {}\n\
            ACK = {}\n\
            SYN = {}\n\
            FIN = {}\n\
            window = {}\n\
            checksum = {}\n\
            urg_ptr = {}",
            (uint16_t)seg.src_port, (uint16_t)seg.dst_port, (uint32_t)seg.seq, (uint32_t)seg.ack, (size_t)seg.header_len, (size_t)seg.padding, (bool)seg.ACK, (bool)seg.SYN, (bool)seg.FIN, (uint16_t)seg.window, (uint16_t)seg.checksum, (uint16_t)seg.urg_ptr) << std::endl;
    return os;
}

#endif
