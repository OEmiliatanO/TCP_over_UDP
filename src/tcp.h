#ifndef __TCP_H__
#define __TCP_H__

#include <iostream>
#include <algorithm>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <tcp_para.h>
#include <tcp_struct.h>
#include <tcp_utili.h>

struct tcp_connection
{
    int sock_fd;
    uint16_t seq, ack;
    size_t len_addr_from, len_addr_to;
    struct sockaddr_in addr_from, addr_to;
    bool connected = false;

    // server
    int listen()
    {
        this->len_addr_from = sizeof(this->addr_from);
        this->len_addr_to = sizeof(this->addr_to);

        tcp_segment segment;
        // recv SYN
        ssize_t recv_num = recvfrom(this->sock_fd, (char *)&segment, 20, 0, (struct sockaddr *)&this->addr_to, (socklen_t *)&this->len_addr_to);
        if (recv_num == -1 or !(segment.SYN and !tcp_checksum((void *)&segment, (size_t)segment.header_len * 4)))
            return -1;

        std::cerr << "SYN recv" << std::endl;
        this->seq = INIT_ISN();
        this->ack = segment.seq + 20;
        load_segment(segment);
        segment.ACK = true;
        segment.SYN = true;
        segment.checksum = tcp_checksum((void *)&segment, (size_t)segment.header_len * 4);
        ssize_t send_num;
        while(!(~(send_num = sendto(this->sock_fd, (char *)&segment, 20, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to))));
        std::cerr << "SYN-ACK sent" << std::endl;
        this->seq += 20;
        recv_num = recvfrom(this->sock_fd, (char *)&segment, 20, 0, (struct sockaddr *)&this->addr_to, (socklen_t *)&this->len_addr_to);
        if (segment.ACK and !tcp_checksum((void *)&segment, (size_t)segment.header_len * 4))
        {
            std::cerr << "ACK recv" << std::endl;
            this->ack += segment.header_len * 4;
            return 0;
        }
        return -1;
    }

    // server
    int accept()
    {
        std::cerr << "accept" << std::endl;
        /*
        int res_fd = socket(AF_INET, SOCK_DGRAM, 0);

        if(bind(res_fd, (struct sockaddr *)&this->addr_from, sizeof(this->addr_from)) < 0)
        {
            std::cerr << "bind error" << std::endl;
            return -1;
        }*/
        std::cerr << std::format("current seq = {}, ack = {}", this->seq, this->ack) << std::endl;
        connected = true;
        return this->sock_fd;
    }

    // client
    int connect(sockaddr_in& addr_to)
    {
        std::cerr << "in connect()" << std::endl;
        memcpy((void *)&this->addr_to, (void *)&addr_to, sizeof(this->addr_to));
        this->len_addr_from = sizeof(this->addr_from);
        this->len_addr_to = sizeof(this->addr_to);

        this->seq = INIT_ISN();

        tcp_segment segment;
        memset((void *)&segment, 0, sizeof(segment));
        load_segment(segment);
        segment.SYN = true;
        segment.ack = UNDEFINED;
        segment.checksum = tcp_checksum((void *)&segment, (size_t)segment.header_len * 4);
        
        // send SYN
        ssize_t SYN_send_num;
        std::cerr << "SYN sent" << std::endl;
        while (!(~(SYN_send_num = sendto(this->sock_fd, (char *)&segment, (size_t)segment.header_len * 4, 0, (struct sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to))));
        this->seq += segment.header_len * 4;

        getsockname(this->sock_fd, (struct sockaddr*)&this->addr_from, (socklen_t *)&this->len_addr_from);

        // receive SYN-ACK
        ssize_t recv_num = recvfrom(this->sock_fd, (char *)&segment, 20, 0, (struct sockaddr *)&this->addr_to, (socklen_t *)&this->len_addr_to);
        if (recv_num == -1 or !(segment.ACK and segment.SYN and !tcp_checksum((void *)&segment, (size_t)segment.header_len * 4)))
            return -1;

        std::cerr << "SYN-ACK recv" << std::endl;
        this->ack = segment.seq + 20;

        load_segment(segment);
        segment.ACK = true;
        segment.checksum = tcp_checksum((void *)&segment, (size_t)segment.header_len * 4);

        // send ACK
        ssize_t ACK_send_num;
        std::cerr << "ACK sent" << std::endl;
        while (!(~(ACK_send_num = sendto(this->sock_fd, (char *)&segment, (size_t)segment.header_len * 4, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to))));
        this->seq += segment.header_len * 4;

        std::cerr << std::format("current seq = {}, ack = {}", this->seq, this->ack) << std::endl;
        connected = true;
        return 0;
    }

    void load_segment(tcp_segment& segment)
    {
        memset((void *)&segment, 0, sizeof(segment));
        segment.dst_port = ntohs(this->addr_to.sin_port);
        segment.src_port = ntohs(this->addr_from.sin_port);
        segment.seq = this->seq;
        segment.ack = this->ack;
        segment.header_len = 5;
        segment.window = buffer_size / MSS;
        segment.urg_ptr = 0;
    }

    void load_segment(tcp_segment& segment, const void * data, size_t len)
    {
        load_segment(segment);
        memcpy(segment.data, data, std::min(len, MSS));
    }

    ssize_t send(const void* data, size_t len)
    {
        if (not connected) return -1;
        tcp_segment segment;
        load_segment(segment, data, len);
        segment.checksum = tcp_checksum((void *)&segment, (size_t)segment.header_len * 4);
        ssize_t send_num = 0;
        while (!(~(send_num = sendto(this->sock_fd, (char *)&segment, (size_t)segment.header_len * 4 + len, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to))));
        this->seq += segment.header_len * 4 + len;
        return send_num;
    }

    ssize_t recv(void *buffer, size_t len)
    {
        if (not connected) return -1;
        tcp_segment segment;
        memset((void *)&segment, 0, sizeof(segment));
        struct sockaddr_in client;
        socklen_t len_client;
        ssize_t recv_num = recvfrom(this->sock_fd, (char *)&segment, 20 + len, 0, (sockaddr *)&client, (socklen_t *)&this->len_client);
        if (recv_num < 0) return -1;

        //
        this->ack += 20 + recv_num;
        if (segment.FIN and !tcp_checksum((void *)&segment, (size_t)segment.header_len * 4))
        {
            std::cerr << "FIN recv" << std::endl;
            load_segment(segment);
            segment.ACK = true;
            segment.checksum = tcp_checksum((void *)&segment, (size_t)segment.header_len * 4);
            std::cerr << "ACK sent" << std::endl;
            ssize_t send_num;
            while ((send_num = sendto(this->sock_fd, (char *)&segment, (size_t)segment.header_len * 4, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to)) < 0);
            std::cerr << "send FIN" << std::endl;
            connected = false;
            while (close() < 0);
            return 0;
        }
        memcpy(buffer, segment.data, std::min((size_t)recv_num - 20, len));
        return recv_num;
    }

    int close()
    {
        tcp_segment segment;
        load_segment(segment);
        segment.FIN = true;
        segment.checksum = tcp_checksum((void *)&segment, (size_t)segment.header_len * 4);
        ssize_t send_num;
        while ((send_num = sendto(this->sock_fd, (char *)&segment, segment.header_len * 4, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to)) < 0);
        ssize_t recv_num = recvfrom(sock_fd, (char *)&segment, 20, 0, (sockaddr *)&this->addr_to, (socklen_t *)&this->len_addr_to);
        if (!(segment.ACK and !tcp_checksum((void *)&segment, (size_t)segment.header_len * 4)))
            return -1;
        std::cerr << "closed" << std::endl;
        
        if (not connected) return 0;

        recv_num = recvfrom(sock_fd, (char *)&segment, 20, 0, (sockaddr *)&this->addr_to, (socklen_t *)&this->len_addr_to);
        if (!(recv_num >= 0 and segment.FIN and !tcp_checksum((void *)&segment, (size_t)segment.header_len * 4)))
            return -1;

        load_segment(segment);
        segment.ACK = true;
        segment.checksum = tcp_checksum((void *)&segment, (size_t)segment.header_len * 4);

        while ((send_num = sendto(this->sock_fd, (char *)&segment, segment.header_len * 4, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to)) < 0);

        connected = false;
        return 0;
    }

};
#endif
