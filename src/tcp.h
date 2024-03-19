#ifndef __TCP_H__
#define __TCP_H__

#include <iostream>
#include <format>
#include <algorithm>
#include <cstring>
#include <map>
#include <future>
#include <variant>
#include <string_view>
#include <mutex>
#include <condition_variable>

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <utili.h>
#include <tcp_utili.h>
#include <tcp_para.h>
#include <tcp_mux.h>
#include <tcp_struct.h>
#include <tcp_connection.h>

constexpr size_t MAX_CLIENT_NUM = 20;

struct tcp_manager
{
    using addrs_t = std::pair<std::string, std::string>;

    int sock_fd;
    size_t client_num; 
    tcp_segment tmp_segment;
    size_t len_addr_from, len_addr_to;
    sockaddr_in addr_from, addr_to;
    bool is_server_side;

    int mux_thread_id;
    MUX::tcp_multiplexer mux;
    std::future<int> get_thread_id;

    std::map<addrs_t, int> mapping;
    std::map<int, std::thread> threads;
    std::map<int, tcp_connection> connections;
    std::map<int, bool> used;

    tcp_manager(int _sock_fd): sock_fd{_sock_fd}
    {
        this->len_addr_from = sizeof(this->addr_from);
        this->len_addr_to = sizeof(this->addr_to);
        client_num = 0;
        is_server_side = false;
        bind(_sock_fd);
    }

    void bind(int _sock_fd)
    {
        mux.sock_fd = _sock_fd;
    }

    void multiplex()
    { 
        constexpr size_t _PACKET = 0;
        while (true)
        {
            auto packet = mux._recv();
            if (packet.index() == _PACKET)
            {
                auto [recv_num, segment, client] = std::get<_PACKET>(packet);
                auto key = std::make_pair(sockaddr_to_string(this->addr_from), sockaddr_to_string(client));
                //std::cerr << std::format("Mux thread: Key = [{}, {}]", key.first, key.second) << std::endl;
                //std::cerr << "Mux thread: Receive segment = \n" << segment << std::endl;
                if (auto res = mapping.find(key); res == mapping.end())
                {
                    // recv SYN
                    if (is_server_side and segment.SYN and not corrupt(segment))
                    {
                        if (client_num == MAX_CLIENT_NUM)
                        {
                            std::cerr << "Mux thread: Reach the max number of connected client." << std::endl;
                            continue;
                        }
                        
                        int thread_id;
                        while (used.find(thread_id = gen_id()) != used.end());
                        used[thread_id] = true;
                        mapping[key] = thread_id;
                        std::cerr << std::format("Mux thread: Add [{}, {}] to dict.", sockaddr_to_string(this->addr_from), sockaddr_to_string(client)) << std::endl;
                        connections[thread_id].thread_id = thread_id;
                        connections[thread_id].sock_fd = sock_fd;
                        connections[thread_id].receive_qu.emplace_back(recv_num, segment);
                        connections[thread_id].seq = INIT_ISN(), connections[thread_id].ack = segment.seq + 1;
                        connections[thread_id].addr_from = this->addr_from, connections[thread_id].addr_to = client;
                        connections[thread_id].len_addr_from = sizeof(this->addr_from), connections[thread_id].len_addr_to = sizeof(client);
                        connections[thread_id].header_len = sizeof(segment) - MSS * sizeof(char);
                        
                        std::cerr << std::format("Mux thread: Receive from {}:{} new SYN segment.", 
                                inet_ntoa(client.sin_addr), 
                                ntohs(client.sin_port)) << std::endl;
                        
                        std::promise<int> promise_thread_id;
                        get_thread_id = promise_thread_id.get_future();
                        promise_thread_id.set_value(thread_id);
                    }
                    else
                    {
                        std::cerr << std::format("Mux thread: Receive unknown segment from {}:{}, corruptness = {}", 
                                inet_ntoa(client.sin_addr), 
                                ntohs(client.sin_port), corrupt(segment)) << std::endl;
                    }
                }
                // recv segment for certain thread
                else
                {
                    //std::cerr << std::format("Mux thread: Receive segment from {}:{}:\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port)) << segment << std::endl;
                    auto thread_id = mapping[key];
                    std::lock_guard<std::mutex> lock(connections[thread_id].receive_qu_mutex);
                    connections[thread_id].receive_qu.emplace_back(recv_num, segment);
                    connections[thread_id].rwnd -= (recv_num - segment.header_len * 4);
                    connections[thread_id].receive_qu_cv.notify_one();
                }
            }
        }
        return;
    }

    // server
    void listen()
    {
        is_server_side = true;
        mux_thread_id = gen_id();
        threads[mux_thread_id] = std::thread(&tcp_manager::multiplex, this);
        threads[mux_thread_id].detach();
    }

    // server
    int accept()
    {
        while (not get_thread_id.valid()) std::this_thread::yield();
        auto thread_id = get_thread_id.get();

        std::cerr << "=====Start three-way handshake=====" << std::endl;
        ++client_num;

        tcp_connection& channel = connections[thread_id];

        channel.connected = true;
        
        std::cerr << std::format("thread #{} create", thread_id) << std::endl;

        static tcp_segment segment;
        channel.header_len = sizeof(segment) - MSS * sizeof(char);
        
        ssize_t recv_num = channel.recv_packet(segment);
        if (not (recv_num > 0 and segment.SYN and not corrupt(segment))) return -1;

        // send SYN-ACK
        channel.load_segment(segment);
        segment.ACK = true, segment.SYN = true;
        segment.checksum = tcp_checksum((void *)&segment, (size_t)segment.header_len * 4);
        std::cerr << std::format("thread #{}: Send SYN-ACK packet, {} bytes", thread_id, segment.header_len * 4) << std::endl;
        channel.send_packet(segment);
        channel.seq += 1;

        // receive ACK
        recv_num = channel.recv_packet(segment);
        if (recv_num > 0 and segment.ACK and not corrupt(segment))
        {
            std::cerr << std::format("thread #{}: Receive ACK packet", thread_id) << std::endl;
            std::cerr << std::format("Receive a packet (seq_num = {}, ack_num = {})", (uint16_t)segment.seq, (uint16_t)segment.ack) << std::endl;
            std::cerr << "=====Complete the three-way handshake=====" << std::endl;
            std::cerr << "=====Connection established=====" << std::endl;
            std::cerr << std::format("Current seq = {}, ack = {}", channel.seq, channel.ack) << std::endl;

            return thread_id;
        }
        // source release
        else
        {
            std::cerr << std::format("thread #{}: Error. ACK = {}, Corruptness = {}", thread_id, (bool)segment.ACK, corrupt(segment)) << std::endl;
            std::cerr << std::format("Receive a packet (seq_num = {}, ack_num = {})", (uint16_t)segment.seq, (uint16_t)segment.ack) << std::endl;
            connections.erase(thread_id);
            auto key = std::make_pair(sockaddr_to_string(channel.addr_from), sockaddr_to_string(channel.addr_to));
            mapping.erase(key);
            used.erase(thread_id);
            --client_num;
            return -1;
        }
    }

    // client
    int connect(sockaddr_in& addr_to)
    {
        is_server_side = false;

        int thread_id;
        while (used.find(thread_id = gen_id()) != used.end());
        used[thread_id] = true;

        static tcp_segment segment;
        connections[thread_id].thread_id = thread_id; // for debug
        connections[thread_id].sock_fd = this->sock_fd;
        connections[thread_id].header_len = sizeof(segment) - MSS * sizeof(char);
        connections[thread_id].addr_to = addr_to;
        connections[thread_id].len_addr_from = sizeof(connections[thread_id].addr_from);
        connections[thread_id].len_addr_to = sizeof(connections[thread_id].addr_to);
        connections[thread_id].seq = INIT_ISN();
        connections[thread_id].connected = true;

        tcp_connection& channel = connections[thread_id];
        channel.load_segment(segment);
        segment.SYN = true;
        segment.checksum = tcp_checksum((void *)&segment, (size_t)segment.header_len * 4);
        
        std::cerr << "=====Start the three-way handshake=====" << std::endl;
        // send SYN
        channel.send_packet(segment);
        std::cerr << std::format("Send SYN to {}:{}", inet_ntoa(channel.addr_to.sin_addr), ntohs(channel.addr_to.sin_port)) << std::endl;

        // load local socket information
        // local socket information can be obtained "only after" send the data :(
        getsockname(channel.sock_fd, (struct sockaddr*)&channel.addr_from, (socklen_t *)&channel.len_addr_from);
        this->addr_from = channel.addr_from;
        this->len_addr_from = channel.len_addr_from;
        auto key = std::make_pair(sockaddr_to_string(channel.addr_from), sockaddr_to_string(channel.addr_to));
        mapping[key] = thread_id;

        // create mux thread to receive packet
        mux_thread_id = gen_id();
        threads[mux_thread_id] = std::thread(&tcp_manager::multiplex, this);
        threads[mux_thread_id].detach();

        // receive SYN-ACK
        memset((void *)&segment, 0, sizeof(segment));
        std::cerr << "Wait for ACK" << std::endl;
        ssize_t recv_num = channel.recv_packet(segment);
        if (recv_num < 0 or not (segment.ACK and segment.SYN and !tcp_checksum((void *)&segment, (size_t)segment.header_len * 4)))
        {
            if (recv_num < 0)
                std::cerr << "recv_num < 0" << std::endl;
            else
                std::cerr << "not recv ACK" << std::endl;
            mapping.erase(key);
            connections.erase(thread_id);
            threads.clear();
            return -1;
        }

        std::cerr << std::format("Receive a packet (SYN-ACK) from {}:{}", inet_ntoa(channel.addr_to.sin_addr), ntohs(channel.addr_to.sin_port));
        std::cerr << std::format("\tReceive a packet (seq_num = {}, ack_num = {})", (uint16_t)segment.seq, (uint16_t)segment.ack) << std::endl;
        channel.seq = segment.ack;
        channel.ack = segment.seq + 1;

        // send ACK
        channel.load_segment(segment);
        segment.ACK = true;
        segment.checksum = tcp_checksum((void *)&segment, (size_t)segment.header_len * 4);

        std::cerr << std::format("Send a packet(ACK) to {}:{}", inet_ntoa(channel.addr_to.sin_addr), ntohs(channel.addr_to.sin_port)) << std::endl;
        channel.send_packet(segment);

        std::cerr << "=====Complete the three-way handshake=====" << std::endl;
        std::cerr << "=====Connection established=====" << std::endl;
        std::cerr << std::format("Current seq = {}, ack = {}", channel.seq, channel.ack) << std::endl;
        ++client_num;

        return thread_id;
    }

    void release(int id)
    {
        auto key = std::make_pair(sockaddr_to_string(connections[id].addr_from), sockaddr_to_string(connections[id].addr_to));
        mapping.erase(key);
        used.erase(id);
        connections.erase(id);
        --client_num;
    }
};

#endif
