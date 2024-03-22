#ifndef __TCP_MUX_H__
#define __TCP_MUX_H__

#include <tuple>
#include <variant>

#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <tcp_struct.h>

namespace tcp_mux
{
    using packet_t = std::tuple<ssize_t, tcp_struct::segment, sockaddr_in>;
    struct multiplexer
    {
        int sock_fd;

        std::variant<packet_t, ssize_t> _recv()
        {
            static sockaddr_in client;
            static socklen_t len_client = sizeof(client);
            static tcp_struct::segment segment;
            segment.clear();

            ssize_t recv_num = recvfrom(sock_fd, (char *)&segment, sizeof(segment), 0, (sockaddr *)&client, (socklen_t *)&len_client);
            if (recv_num < 0) return recv_num;
            
            return std::make_tuple(recv_num, segment, client);
        }
    };
}

#endif
