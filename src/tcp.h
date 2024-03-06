#ifndef __TCP_H__
#define __TCP_H__

#include <iostream>
#include <algorithm>
#include <cstring>
#include <map>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>

#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <utili.h>
#include <tcp_para.h>
#include <tcp_struct.h>
#include <tcp_utili.h>

using addrs_t = std::pair<std::string, std::string>;
using packet_t = std::pair<ssize_t, tcp_segment>;

constexpr int NEW_CLIENT_SYN = 1;
constexpr size_t MAX_CLIENT_NUM = 20;

struct tcp_connection
{
    int sock_fd;
    uint16_t seq, ack;
    size_t len_addr_from, len_addr_to;
    sockaddr_in addr_from, addr_to;
    bool connected = false;

    size_t header_len; // byte

    std::queue<packet_t> qu;
    std::vector<packet_t> buffer;
    std::mutex mutex;
    std::condition_variable cv;

    tcp_connection() = default;
    tcp_connection(const tcp_connection& other)
    {
        this->sock_fd = other.sock_fd;
        this->seq = other.seq;
        this->ack = other.ack;
        this->len_addr_from = other.len_addr_from;
        this->len_addr_to = other.len_addr_to;
        this->addr_from = other.addr_from, this->addr_to = other.addr_to;
        this->connected = other.connected;
    }

    void load_segment(tcp_segment& segment)
    {
        memset((void *)&segment, 0, sizeof(segment));
        segment.dst_port = ntohs(this->addr_to.sin_port);
        segment.src_port = ntohs(this->addr_from.sin_port);
        segment.seq = this->seq;
        segment.ack = this->ack;
        segment.header_len = this->header_len / 4;
        segment.window = buffer_size / MSS;
        segment.urg_ptr = 0;
    }

    void load_segment(tcp_segment& segment, const void * data, size_t len)
    {
        load_segment(segment);
        memcpy(segment.data, data, std::min(len, MSS));
    }
    
    ssize_t send(tcp_segment& segment, size_t len)
    {
        if (not connected) return -1;
        ssize_t send_num;
        while((send_num = sendto(sock_fd, (char *)&segment, len, 0, (sockaddr *)&addr_to, len_addr_to)) < 0)
        {
            std::cerr << errno << std::endl;
        }
        this->seq += (uint16_t)send_num;
        return send_num;
    }

    ssize_t send(const void* data, size_t len)
    {
        if (not connected) return -1;
        static tcp_segment segment;
        load_segment(segment, data, len);
        return send(segment, (size_t)segment.header_len * 4 + len);
    }

    ssize_t recv(tcp_segment& segment)
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return not qu.empty(); });
        packet_t p = qu.front();
        ssize_t recv_num = p.first;
        segment = p.second;
        qu.pop();
        this->ack += (uint16_t)recv_num;
        lock.unlock();
        return recv_num;
    }

    ssize_t recv(void* buf, size_t len)
    {
        tcp_segment segment;
        ssize_t recv_num = recv(segment);
        if (recv_num <= segment.header_len * 4) return 0;
        if (segment.FIN and not corrupt(segment))
        {
            load_segment(segment);
            segment.ACK = true;
            send(segment, (size_t)segment.header_len * 4);
            connected = false;
            close();
        }
        memcpy(buf, segment.data, std::min((size_t)recv_num - segment.header_len * 4, len));
        return recv_num - segment.header_len * 4;
    }

    int close()
    {
        tcp_segment segment;
        load_segment(segment);
        segment.FIN = true;
        send(segment, (size_t)segment.header_len * 4);
        recv(segment);
        if (not (segment.ACK and not corrupt(segment)))
            return -1;
        if (connected)
        {
            recv(segment);
            if (not (segment.FIN and not corrupt(segment)))
                return -1;
            load_segment(segment);
            segment.ACK = true;
            send(segment, (size_t)segment.header_len * 4);
            connected = false;
            return 0;
        }
        return -1;
    }
};

struct tcp_manager
{
    int sock_fd;
    size_t client_num; 
    tcp_segment tmp_segment;
    size_t len_addr_from, len_addr_to;
    sockaddr_in addr_from, addr_to;

    std::function<void(tcp_connection&, int)> server_func;
    std::vector<std::thread> threads;
    std::map<addrs_t, int> mapping;
    std::map<int, tcp_connection> connections;
    std::map<int, bool> used;

    tcp_manager() = default;
    tcp_manager(int _sock_fd, std::function<void(tcp_connection, int)> _server_func): sock_fd{_sock_fd}, server_func{_server_func}
    {
        this->len_addr_from = sizeof(this->addr_from);
        this->len_addr_to = sizeof(this->addr_to);
        client_num = 0;
    }

    void set_server_func(std::function<void(tcp_connection, int)> _server_func)
    {
        server_func = _server_func;
    }

    // server
    int listen()
    {
        while (true)
        {
            int status = _recv();
            if (status == NEW_CLIENT_SYN)
            {
                // ignore
                if (client_num == MAX_CLIENT_NUM)
                {
                    std::cerr << "Reach the max number of connected client." << std::endl;
                    continue;
                }
                std::cerr << "threeway handshake start:" << std::endl;
                ++client_num;


                int thread_id;
                while (used.find((thread_id = gen_id())) != used.end());
                used[thread_id] = true;
                auto key = std::make_pair(sockaddr_to_string(this->addr_from), sockaddr_to_string(this->addr_to));
                mapping[key] = thread_id;
                connections[thread_id].sock_fd = this->sock_fd;
                tcp_connection& channel = connections[thread_id];

                // parameter setting
                channel.addr_from = this->addr_from;
                channel.len_addr_from = sizeof(this->addr_from);
                channel.addr_to = this->addr_to;
                channel.len_addr_to = sizeof(this->addr_to);
                channel.ack = this->tmp_segment.seq + this->tmp_segment.header_len * 4;
                channel.seq = INIT_ISN();
                channel.connected = true;
                
                std::cerr << std::format("thread #{} create", thread_id) << std::endl;
                threads.emplace_back([&](int thread_id){
                        tcp_connection& channel = connections[thread_id];

                        // threeway handshake
                        tcp_segment segment;
                        channel.header_len = sizeof(segment) - MSS;
                        
                        // send SYN-ACK
                        channel.load_segment(segment);
                        segment.ACK = true, segment.SYN = true;
                        segment.checksum = tcp_checksum((void *)&segment, (size_t)segment.header_len * 4);
                        std::cerr << std::format("thread #{}: send SYN-ACK segment, {} bytes", thread_id, segment.header_len * 4) << std::endl;
                        channel.send(segment, segment.header_len * 4);

                        // should recv ACK
                        std::cerr << std::format("thread #{}: wait to recv ACK segment", thread_id) << std::endl;
                        ssize_t recv_num = channel.recv(segment);
                        if (segment.ACK and not corrupt(segment))
                        {
                            // connection established
                            while (connections[thread_id].connected)
                            {
                                // the main server function
                                // user custom
                                server_func(connections[thread_id], thread_id);
                            }
                        }

                        std::cerr << std::format("thread #{}: close connection", thread_id) << std::endl;
                        // source release
                        connections.erase(thread_id);
                        auto key = std::make_pair(sockaddr_to_string(channel.addr_from), sockaddr_to_string(channel.addr_to));
                        mapping.erase(key);
                        used.erase(thread_id);
                        --client_num;
                    }, thread_id);
                threads.back().detach();
            }
        }
    }

    // client
    int connect(sockaddr_in& addr_to)
    {
        int thread_id = gen_id();
        tcp_segment segment;
        connections[thread_id].sock_fd = this->sock_fd;
        tcp_connection& channel = connections[thread_id];
        channel.header_len = sizeof(segment) - MSS * sizeof(char);
        channel.addr_to = addr_to;
        channel.len_addr_from = sizeof(channel.addr_from);
        channel.len_addr_to = sizeof(channel.addr_to);
        channel.seq = INIT_ISN();
        channel.connected = true;

        channel.load_segment(segment);
        segment.SYN = true;
        segment.ack = UNDEFINED;
        segment.checksum = tcp_checksum((void *)&segment, (size_t)segment.header_len * 4);
        
        // send SYN
        ssize_t send_num;
        send_num = channel.send(segment, (size_t)segment.header_len * 4);
        std::cerr << "SYN sent" << std::endl;

        getsockname(this->sock_fd, (struct sockaddr*)&channel.addr_from, (socklen_t *)&channel.len_addr_from);
        this->addr_from = channel.addr_from;
        this->len_addr_from = channel.len_addr_from;

        // receive SYN-ACK
        auto key = std::make_pair(sockaddr_to_string(channel.addr_from), sockaddr_to_string(channel.addr_to));
        mapping[key] = thread_id;
        _recv();
        ssize_t recv_num = channel.recv(segment);
        if (recv_num < 0 or not (segment.ACK and segment.SYN and !tcp_checksum((void *)&segment, (size_t)segment.header_len * 4)))
        {
            if (recv_num < 0)
                std::cerr << "recv_num < 0" << std::endl;
            else
                std::cerr << "not recv ACK" << std::endl;
            mapping.erase(key);
            connections.erase(thread_id);
            return -1;
        }

        std::cerr << "SYN-ACK recv" << std::endl;
        channel.ack = segment.seq + recv_num;

        channel.load_segment(segment);
        segment.ACK = true;
        segment.checksum = tcp_checksum((void *)&segment, (size_t)segment.header_len * 4);

        // send ACK
        std::cerr << "ACK sent" << std::endl;
        send_num = channel.send(segment, (size_t)segment.header_len * 4);

        std::cerr << std::format("current seq = {}, ack = {}", channel.seq, channel.ack) << std::endl;
        return thread_id;
    }

    int _recv()
    {
        sockaddr_in client;
        socklen_t len_client = sizeof(client);
        ssize_t recv_num = recvfrom(this->sock_fd, (char *)&this->tmp_segment, 24 + MSS, 0, (sockaddr *)&client, (socklen_t *)&len_client);
        if (recv_num < 0) return -1;
        auto key = std::make_pair(sockaddr_to_string(this->addr_from), sockaddr_to_string(client));
        if (auto res = mapping.find(key); res == mapping.end())
        {
            // recv SYN
            if (this->tmp_segment.SYN and not corrupt(this->tmp_segment))
            {
                std::cerr << "recv new client SYN segment" << std::endl;
                this->addr_to = client;
                return NEW_CLIENT_SYN;
            }
            // recv segment belongs to no one
            std::cerr << "recv segment belongs to no one, corruptness = " << corrupt(this->tmp_segment) << std::endl;
            std::cerr << this->tmp_segment << std::endl;
            return -1;
        }
        // recv segment for certain thread
        auto thread_id = mapping[key];
        std::lock_guard<std::mutex> lock(connections[thread_id].mutex);
        connections[thread_id].qu.emplace(recv_num, this->tmp_segment);
        connections[thread_id].cv.notify_one();
        memset((void *)&this->tmp_segment, 0, sizeof(this->tmp_segment));
        return 0;
    }

    ssize_t send(int id, void* data, size_t len)
    {
        return connections[id].send(data, len);
    }

    ssize_t recv(int id, void* buffer, size_t len)
    {
        _recv();
        return connections[id].recv(buffer, len);
    }

    int close(int id)
    {
        return connections[id].close();
    }

};
#endif
