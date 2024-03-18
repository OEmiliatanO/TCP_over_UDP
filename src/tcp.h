#ifndef __TCP_H__
#define __TCP_H__

#include <iostream>
#include <algorithm>
#include <cstring>
#include <map>
#include <queue>
#include <functional>
#include <future>
#include <variant>
#include <chrono>
#include <mutex>
#include <condition_variable>

#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <utili.h>
#include <tcp_para.h>
#include <tcp_mux.h>
#include <tcp_struct.h>
#include <tcp_utili.h>

constexpr size_t MAX_CLIENT_NUM = 20;
constexpr ssize_t CLOSE_SIGNAL = -2;

struct tcp_connection
{
    using packet_t = std::pair<ssize_t, tcp_segment>;
    using send_packet_t = std::pair<size_t, tcp_segment>;
    using namespace std::chrono_literals;

    int sock_fd;
    seq_t seq, ack;
    seq_t acked;
    size_t len_addr_from, len_addr_to;
    sockaddr_in addr_from, addr_to;

    volatile size_t rwnd, cwnd;
    volatile bool connected;
    size_t ssthresh;

    size_t header_len; // byte

    // receive all packet (including ACK, FIN) from tcp mux
    std::map<seq_t, packet_t> receive_window;
    std::map<seq_t, size_t> ack_counter;
    // receive data packet (not including ACK, FIN)
    std::deque<packet_t> recv_qu;

    std::mutex receive_window_mutex;
    std::condition_variable receive_window_cv;

    tcp_connection()
    {
        this->len_addr_from = sizeof(this->addr_from);
        this->len_addr_to = sizeof(this->addr_to);
        this->rwnd = buffer_size;
        this->cwnd = MSS;
        this->connected = false;
        this->ssthresh = threshold;
    }

    void load_segment(tcp_segment& segment)
    {
        segment.clear();
        segment.dst_port = ntohs(this->addr_to.sin_port);
        segment.src_port = ntohs(this->addr_from.sin_port);
        segment.seq = this->seq;
        segment.ack = this->ack;
        segment.header_len = this->header_len / 4;
        segment.window = this->rwnd;
        segment.urg_ptr = 0;
    }

    void load_segment(tcp_segment& segment, const void * data, size_t len)
    {
        load_segment(segment);
        memcpy(segment.data, data, std::min(len, MSS));
    }

    void start_receiver()
    {
        //receiver = std::thread(tcp_connection::_recv, channel);
        //receiver.detach();
    }
    void start_sender()
    {
        //sender = std::thread(tcp_connection::_send, channel);
        //sender.detach();
    }

    // the value of this->seq doesn't change when sending a packet contains no data
    ssize_t send_packet(tcp_segment& segment)
    {
        ssize_t send_num;
        while ((send_num = sendto(this->sock_fd, (char *)&segment, segment.header_len * 4, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to)) < 0);
        return send_num;
    }
    // the value of this->ack doesn't change when receiving a packet contains no data
    ssize_t recv_packet(tcp_segment& segment)
    {
        auto [recv_seq, recv_packet] = *receive_window.begin(); receive_window.erase(receive_window.begin());
        auto [recv_num, segment] = recv_packet;
        return recv_num;
    }

    // for sender thread, 
    // only sends the segment WITH data from application layer
    /*
    void _send()
    {
        auto sent_itor = send_qu.begin();
        auto acked_itor = send_qu.begin();
        while (true)
        {
            std::lock_guard<std::mutex> lock(send_qu_mutex);
            send_qu_cv.wait(lock, [&] { return not send_qu.empty() });

            while (sent_itor != send_qu.end() and (*sent_itor).second.seq - (*acked_itor).second.seq + (*sent_itor).first <= std::min(rwnd, cwnd))
            {
                auto [len, segment] = *send_itor++;
                while (sendto(this->sock_fd, (char *)&segment, len, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to) < 0);
            }

            while (not send_qu.empty() and (*acked_itor).second.seq < this->acked)
            {
                ++acked_itor;
                send_qu.pop_front();
            }
            if (send_qu.empty())
                sent_itor = acked_itor = send_qu.begin();
        }
    }
    */

    // for receiver
    // this function doesn't modify this->seq
    /*
    void _recv()
    {
        while (true)
        {
            // object used: 
            // - receive_window: std::map
            // - ack: seq_t
            // - acked: seq_t
            // - cwnd: size_t
            // - recv_qu: std::deque
            // function used:
            // - send
            std::unique_lock<std::mutex> lock(receive_window_mutex);
            receive_window_cv.wait(lock, [&] { return not receive_window.empty(); });

            auto it = receive_window.find(this->ack);
            if (it == receive_window.end())
            {
                std::this_thread::yield();
                continue;
            }

            auto [recv_num, recv_segment] = *it;
            receive_window.erase(it);

            lock.unlock();

            if (corrupt(recv_segment) or recv_num < 0) continue;

            auto data_len = recv_num - recv_segment.header_len * 4;
            this->ack += data_len;

            if (recv_segment.ACK)
            {
                if (recv_segment.ack > acked)
                {
                    this->acked = recv_segment.ack;
                    this->rwnd = recv_segment.window;
                }
                this->cwnd <<= 1; // congestion control TODO
            }
            
            if (data_len == 0) continue;

            // recv_qu part
            std::unique_lock<std::mutex> recv_qu_lock{recv_qu_mutex};
            recv_qu.emplace_back(recv_num, recv_segment);
            recv_qu_lock.unlock();
            recv_qu_cv.notify_one();

            // quickly send an ACK
            load_segment(segment);
            segment.ACK = true;
            segment.checksum = tcp_checksum((void *)&segment, segment.header_len * 4);
            send_packet(segment);
        }
    }
    */

    void timeout_trans()
    {
        this->congestion_state = SLOW_START;
        this->ssthresh = this->cwnd >> 1;
        this->cwnd = MSS;
    }

    void newACK_trans()
    {
        if (this->congestion_state == SLOW_START)
        {
            this->cwnd += MSS;
            if (this->cwnd >= ssthresh) this->congestion_state = CONGESTION_AVOIDANCE;
        }
        else if (this->congestion_state == CONGESTION_AVOIDANCE)
            this->cwnd += MSS * MSS / this->cwnd;
        else
        {
            this->congestion_state = CONGESTION_AVOIDANCE;
            this->cwnd = this->ssthresh;
        }
    }

    void dup3ACK_trans()
    {
        this->congestion_state = FAST_RECOVERY;
        this->ssthresh = this->cwnd >> 1;
        this->cwnd = this->ssthresh + 3 * MSS;
    }

    std::mutex create_qu_mutex;
    std::deque<packet_t> create_send_qu(const void* data, size_t len)
    {
        std::lock_guard<std::mutex> lock{create_qu_mutex};

        std::deque<packet_t> send_qu;
        size_t _len = len;
        while (_len > 0)
        {
            tcp_segment segment;
            size_t sending_size = std::min(_len, MSS);
            load_segment(segment, data, sending_size);
            segment.ACK = true;
            segment.checksum = tcp_checksum((void *)&segment, segment.header_len * 4);
            
            //std::cerr << "Send packet" << std::endl;
            //std::cerr << "data[0] = " << (int)((char *)data)[0] << std::endl;
            //std::lock_guard<std::mutex> lock(send_qu_mutex);
            //send_qu_cv.wait(lock, [&] { return not send_qu.empty() });
            send_qu.emplace_back((size_t)segment.header_len * 4 + sending_size, segment);
            
            this->seq += sending_size;
            _len -= sending_size;
        }

        return send_qu;
    }

    // user API
    ssize_t send(const void* data, size_t len)
    {
        if (not connected or len == 0) return -1;

        std::deque<packet_t> send_qu = create_send_qu(data, len);

        auto sent_itor = send_qu.begin();
        auto acked_itor = send_qu.begin();
        size_t to_send_num = 0;
        size_t total_send = 0;
        std::chrono::steady_clock::time_point st;
        auto timer_running = false;
        auto timeout = 1000ms;
        auto loss_tested = false;
        std::map<seq_t, size_t> ack_counter;
        std::cerr << std::format("In send(), start transmit data with {} bytes", len) << std::endl;
        while (not send_qu.empty())
        {
            // send segments
            while (sent_itor != send_qu.end() 
                    and (to_send_num + (*sent_itor).first - (*send_itor).second.header_len * 4) <= std::min(rwnd, cwnd))
            {
                to_send_num += (*sent_itor).first - (*send_itor).second.header_len * 4;
                total_send += (*sent_itor).first - (*send_itor).second.header_len * 4;
                auto [send_num, segment] = *send_itor++;

                // packet lost with 1e-6 probability
                if (get_random() <= 1)
                    continue;
                // paclet lost at byte 4096
                if (not loss_tested and total_send >= 4096)
                {
                    loss_tested = true;
                    continue;
                }

                while (sendto(this->sock_fd, (char *)&segment, send_num, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to) < 0)
                    std::cerr << "In send(), sendto() errno: " << errno << std::endl;
            }

            if (not timer_running)
            {
                timer_running = true;
                st = std::chrono::steady_clock::now();
            }

            // receive ACKs
            std::unique_lock<std::mutex> lock(receive_window_mutex);
            receive_window_cv.wait(lock, [&] { return not receive_window.empty(); });

            auto [recv_seq, recv_packet] = *receive_window.begin();
            receive_window.erase(receive_window.begin());

            lock.unlock();

            ++ack_counter[recv_seq];
            if (ack_counter[recv_seq] >= 2 and congestion_state == FAST_RECOVERY)
                cwnd += MSS;
            // fast retransmit
            if (ack_counter[recv_seq] >= 3)
            {
                auto [send_num, segment] = *acked_itor;
                while (sendto(this->sock_fd, (char *)&segment, send_num, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to) < 0);
                ack_counter.erase(recv_seq);
                
                dup3ACK_trans();
            }

            auto [recv_num, recv_segment] = recv_packet;
            // new ACK
            if (recv_segment.ACK and ack_counter[recv_seq] <= 1)
            {
                if (recv_segment.ack > this->acked)
                {
                    this->acked = recv_segment.ack;
                    this->rwnd = recv_segment.window;
                }
                
                newACK_trans();
                ack_counter.clear();
            }
            
            while (not send_qu.empty() and (*acked_itor).second.seq < this->acked)
            {
                if (timer_running)
                {
                    auto sample_RTT = std::chrono::steady_clock::now() - st;
                    this->estimate_RTT = (1-this->alpha) * this->estimate_RTT + this->alpha * sample_RTT;
                    this->dev_RTT = (1-this->beta) * this->dev_RTT + this->beta * abs(sample_RTT - this->estimate_RTT);
                    this->timeout = this->estimate_RTT + 4 * this->dev_RTT;
                    timer_running = false;
                }

                to_send_num -= (*acked_itor).first - (*acked_itor).second.header_len * 4;
                ++acked_itor;
                send_qu.pop_front();
            }

            // timeout
            if (timer_running and std::chrono::steady_clock::now() - st >= timeout)
            {
                auto [send_num, segment] = *acked_itor;
                while (sendto(this->sock_fd, (char *)&segment, send_num, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to) < 0);
                timeout *= 2;

                timeout_trans();
                ack_counter.clear();
            }
        }

        return len;
    }

    // user API
    ssize_t recv(void* buf, size_t len)
    {
        if (not connected) return -1;

        size_t _len = len;
        size_t p_buf = 0;
        ssize_t ret_recv_num = 0;

        auto timer_running = false;
        auto counter = 0;
        std::chrono::steady_clock::time_point st;
        while (_len > 0)
        {
            //std::unique_lock<std::mutex> lock{recv_qu_mutex};
            //recv_qu_cv.wait(lock, [&] { return not recv_qu.empty(); });
            std::unique_lock<std::mutex> lock(receive_window_mutex);
            receive_window_cv.wait(lock, [&] { return not receive_window.empty(); });

            auto [recv_seq, recv_packet] = *receive_window.begin();
            receive_window.erase(receive_window.begin());

            lock.unlock();

            auto [recv_num, recv_segment] = recv_packet;

            //auto [recv_num, recv_segment] = recv_qu.front(); recv_qu.pop_front();
            auto data_len = recv_num - recv_segment.header_len * 4;
            this->rwnd += data_len, this->ack += data_len;

            if (recv_num < 0) continue;

            if (recv_segment.FIN)
            {
                connected = false;

                std::cerr << "Receive FIN" << std::endl;
                this->ack = recv_segment.seq + 1;

                close();
                return -1;
            }

            if (recv_segment.ACK)
                this->acked = std::max(this->acked, recv_segment.ack);

            if (data_len > 0)
            {
                ++counter;
                if (not timer_running)
                {
                    st = std::chrono::steady_clock::now();
                    timer_running = true;
                }
                else if (std::chrono::steady_clock::now() - st > 600ms or counter >= 2)
                {
                    timer_running = false;
                    counter = 0;
                    load_segment(segment);
                    segment.ACK = true;
                    segment.checksum = tcp_checksum((void *)&segment, segment.header_len * 4);
                    send_packet(segment);
                }
            }

            auto recving_size = std::min((size_t)recv_num - recv_segment.header_len * 4, _len);
            memcpy((char *)buf + p_buf, segment.data, recving_size);
            _len -= recving_size, p_buf += recving_size, ret_recv_num += recving_size;
        }

        timer_running = false;

        return ret_recv_num;
    }

    int close()
    {
        static tcp_segment segment;
        load_segment(segment);
        segment.FIN = true;
        segment.checksum = tcp_checksum((void *)&segment, segment.header_len * 4);
        //std::cerr << "the segment sent:\n" << segment << std::endl;
        send_packet(segment);
        std::cerr << "Send FIN" << std::endl;
        std::cerr << "Wait for ACK" << std::endl;
        recv_packet(segment);
        if (not (segment.ACK and not corrupt(segment)))
            return -1;
        std::cerr << "Received" << std::endl;

        if (connected)
        {
            std::cerr << "Wait for FIN" << std::endl;
            recv_packet(segment);
            if (not (segment.FIN and not corrupt(segment)))
                return -1;
            this->ack = segment.seq + 1;
            load_segment(segment);
            segment.ACK = true;
            send_packet(segment);
            connected = false;
            return 0;
        }
        return -1;
    }
};

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
                        connections[thread_id].sock_fd = sock_fd;
                        connections[thread_id].receive_window.emplace(segment.seq, tcp_channel::packet_t{recv_num, segment});
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
                    auto thread_id = mapping[key];
                    std::lock_guard<std::mutex> lock(connections[thread_id].receive_window_mutex);
                    connections[thread_id].receive_window.emplace(segment.seq, tcp_channel::packet_t{recv_num, segment});
                    connections[thread_id].rwnd -= recv_num;
                    connections[thread_id].receive_window_cv.notify_one();
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

        // receive ACK
        recv_num = channel.recv_packet(segment);
        if (recv_num > 0 and segment.ACK and not corrupt(segment))
        {
            std::cerr << std::format("thread #{}: Receive ACK packet", thread_id) << std::endl;
            std::cerr << std::format("Receive a packet (seq_num = {}, ack_num = {})", (uint16_t)segment.seq, (uint16_t)segment.ack) << std::endl;
            std::cerr << "=====Complete the three-way handshake=====" << std::endl;
            std::cerr << "=====Connection established=====" << std::endl;
            std::cerr << std::format("Current seq = {}, ack = {}", channel.seq, channel.ack) << std::endl;

            channel.start_receiver();
            channel.start_sender();
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
        std::cerr << std::format("\tSend {} bytes", send_num) << std::endl;

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
        send_num = channel.send_packet(segment);
        std::cerr << std::format("\tSend {} bytes", send_num) << std::endl;

        std::cerr << "=====Complete the three-way handshake=====" << std::endl;
        std::cerr << "=====Connection established=====" << std::endl;
        std::cerr << std::format("Current seq = {}, ack = {}", channel.seq, channel.ack) << std::endl;
        ++client_num;

        channel.start_receiver();
        channel.start_sender();

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
