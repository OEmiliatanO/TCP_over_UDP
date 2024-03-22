#ifndef __TCP_CONNECTION_H__
#define __TCP_CONNECTION_H__

#include <iostream>
#include <format>
#include <algorithm>
#include <cstring>
#include <map>
#include <queue>
#include <chrono>
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
#include <tcp_struct.h>


namespace tcp_connection
{
    using namespace std::chrono_literals;
    using packet_t = std::pair<ssize_t, tcp_struct::segment>;
    
    constexpr ssize_t CLOSE_SIGNAL = -2;

    constexpr int SLOW_START = 1;
    constexpr int CONGESTION_AVOIDANCE = 2;

    struct connection
    {
        int thread_id;

        int sock_fd;
        tcp_struct::seq_t seq, ack;
        tcp_struct::seq_t acked;
        size_t len_addr_from, len_addr_to;
        sockaddr_in addr_from, addr_to;

        volatile size_t rwnd, cwnd;
        volatile bool connected;
        size_t ssthresh;
        int congestion_state;

        size_t header_len; // byte

        // receive all packet from tcp mux
        std::deque<packet_t> receive_qu;

        std::mutex receive_qu_mutex;
        std::condition_variable receive_qu_cv;

        connection()
        {
            this->len_addr_from = sizeof(this->addr_from);
            this->len_addr_to = sizeof(this->addr_to);
            this->rwnd = buffer_size;
            this->cwnd = MSS;
            this->connected = false;
            this->ssthresh = threshold;
            this->congestion_state = SLOW_START;
        }

        void load_segment(tcp_struct::segment& segment)
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

        void load_segment(tcp_struct::segment& segment, const void * data, size_t len)
        {
            load_segment(segment);
            memcpy(segment.data, data, std::min(len, MSS));
        }

        // the value of this->seq doesn't change when sending a packet contains no data
        ssize_t send_packet(tcp_struct::segment& segment)
        {
            ssize_t send_num;
            std::this_thread::sleep_for(10ms);
            while ((send_num = sendto(this->sock_fd, (char *)&segment, segment.header_len * 4, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to)) < 0)
            {
                std::cerr << std::format("thread #{}: sendto() errno: ", thread_id) << errno << std::endl;
            }
            return send_num;
        }
        // the value of this->ack doesn't change when receiving a packet contains no data
        ssize_t recv_packet(tcp_struct::segment& segment)
        {
            std::unique_lock<std::mutex> lock(receive_qu_mutex);
            receive_qu_cv.wait(lock, [&] { return not receive_qu.empty(); });

            auto recv_packet = receive_qu.front(); receive_qu.pop_front();

            lock.unlock();

            auto [recv_num, recv_segment] = recv_packet;
            segment = recv_segment;
            return recv_num;
        }

        const std::string_view congestion_state_table[4]{"", "slow start", "congestion avoidance","fast recovery"};
        void timeout_trans()
        {
            std::cerr << std::format("====thread #{}: Timeout: {} -> slow start====", thread_id, congestion_state_table[congestion_state]) << std::endl;
            this->congestion_state = SLOW_START;
            this->ssthresh = this->cwnd >> 1;
            this->cwnd = MSS;
        }

        void newACK_trans()
        {
            std::cerr << std::format("====thread #{}: New ACK: {} -> ", thread_id, congestion_state_table[congestion_state]);
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
            std::cerr << std::format("{}====", congestion_state_table[congestion_state]) << std::endl;
        }

        std::mutex create_qu_mutex;
        std::deque<packet_t> create_send_qu(const void* data, size_t len)
        {
            std::lock_guard<std::mutex> lock{create_qu_mutex};

            std::deque<packet_t> send_qu;
            size_t _len = len;
            size_t p = 0;
            while (_len > 0)
            {
                tcp_struct::segment segment;
                size_t sending_size = std::min(_len, MSS);
                load_segment(segment, (void *)((char *)data+p), sending_size);
                segment.ACK = true;
                segment.checksum = tcp_checksum((void *)&segment, segment.header_len * 4);
                
                send_qu.emplace_back((size_t)segment.header_len * 4 + sending_size, segment);
                
                this->seq += sending_size;
                p += sending_size;
                _len -= sending_size;
            }

            return send_qu;
        }

        const double alpha = 0.125;
        const double beta = 0.25;
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
            auto timeout = 1000.0ms, estimate_RTT = 0.0ms, dev_RTT = 0.0ms;
            auto loss_tested = false;
            std::map<tcp_struct::seq_t, size_t> ack_counter;
            std::cerr << std::format("\nIn send(), start transmit data with {} bytes", len) << std::endl;
            while (not send_qu.empty())
            {
                // send segments
                std::cerr << std::format("thread #{}: expect {} bytes in receiver's buffer", thread_id, to_send_num) << std::endl;
                std::cerr << std::format("  {} packets remain in sending queue", send_qu.size()) << std::endl;
                //std::cerr << std::format("  packet next is {} bytes", (*sent_itor).first - (*sent_itor).second.header_len * 4) << std::endl;
                while (sent_itor != send_qu.end() 
                        and (to_send_num + (*sent_itor).first - (*sent_itor).second.header_len * 4) <= std::min(rwnd, cwnd))
                {
                    to_send_num += (*sent_itor).first - (*sent_itor).second.header_len * 4;
                    total_send += (*sent_itor).first - (*sent_itor).second.header_len * 4;
                    
                    std::cerr << std::format("thread #{}: current total send byte: {}", thread_id, total_send) << std::endl;
                    std::cerr << std::format("  seq = {}, ack = {}", 
                            (tcp_struct::seq_t)(*sent_itor).second.seq, (tcp_struct::seq_t)(*sent_itor).second.ack) << std::endl;
                    std::cerr << std::format("  congestion state = {}, cwnd = {}, rwnd = {}, threshold = {}", 
                            congestion_state_table[congestion_state], cwnd, rwnd, ssthresh) << std::endl;

                    auto [send_num, segment] = *sent_itor++;

                    // packet lost with 1e-6 probability
                    if (get_random() <= 1)
                    {
                        std::cerr << std::format("thread #{}: lose packet, seq = {}, ack = {}", 
                                thread_id, (tcp_struct::seq_t)(*(sent_itor-1)).second.seq, (tcp_struct::seq_t)(*(sent_itor-1)).second.ack) << std::endl;
                        continue;
                    }
                    // paclet lost at byte 4096
                    if (not loss_tested and total_send >= 8192)
                    {
                        std::cerr << std::format("thread #{}: lose packet, seq = {}, ack = {}", 
                                thread_id, (tcp_struct::seq_t)(*(sent_itor-1)).second.seq, (tcp_struct::seq_t)(*(sent_itor-1)).second.ack) << std::endl;
                        loss_tested = true;
                        continue;
                    }
                    
                    std::this_thread::sleep_for(10ms);
                    while (sendto(this->sock_fd, (char *)&segment, send_num, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to) < 0)
                        std::cerr << std::format("thread #{}: In send(), sendto() errno: ", thread_id) << errno << std::endl;
                }

                if (not timer_running)
                {
                    timer_running = true;
                    st = std::chrono::steady_clock::now();
                }

                // receive ACKs
                std::cerr << std::format("thread #{}: Wait for ACK", thread_id) << std::endl;
                tcp_struct::segment recv_segment;
                recv_packet(recv_segment);
                tcp_struct::seq_t recv_ack = recv_segment.ack;

                std::cerr << std::format("thread #{}: Receive segment, seq = {}, ack = {}", 
                        thread_id, (tcp_struct::seq_t)recv_segment.seq, (tcp_struct::seq_t)recv_segment.ack) << std::endl;

                ++ack_counter[recv_ack];

                // fast retransmit
                if (ack_counter[recv_ack] >= 3)
                {
                    std::cerr << std::format("thread #{}: Receive 3 duplicate ACKs, ack = {}, retransmit packet with seq = {}, ack = {}", 
                            thread_id, recv_ack, (tcp_struct::seq_t)(*acked_itor).second.seq, (tcp_struct::seq_t)(*acked_itor).second.ack) << std::endl;
                    auto [send_num, segment] = *acked_itor;
                    while (sendto(this->sock_fd, (char *)&segment, send_num, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to) < 0);
                    ack_counter.erase(recv_ack);
                }

                // new ACK
                if (recv_segment.ACK and ack_counter[recv_ack] <= 1)
                {
                    if (recv_segment.ack > this->acked)
                    {
                        this->acked = recv_segment.ack;
                        this->rwnd = recv_segment.window;
                    }
                    
                    newACK_trans();
                    ack_counter.clear();
                    ack_counter[recv_ack] = 1;
                }
                
                while (not send_qu.empty() and (*acked_itor).second.seq < this->acked)
                {
                    std::cerr << std::format("thread #{}: acked_itor.seq = {} < acked = {}, pop out.", 
                            thread_id, (tcp_struct::seq_t)(*acked_itor).second.seq, this->acked) << std::endl;
                    if (timer_running)
                    {
                        auto sample_RTT = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - st);
                        estimate_RTT = (1-alpha) * estimate_RTT + alpha * sample_RTT;
                        dev_RTT = (1-beta) * dev_RTT + beta * abs(sample_RTT - estimate_RTT);
                        timeout = estimate_RTT + 4 * dev_RTT;
                        std::cerr << std::format("thread #{}: sample RTT = {}, \n\testimate RTT = {}, dev RTT = {}, timeout interval = {}", 
                                thread_id, sample_RTT.count(), estimate_RTT.count(), dev_RTT.count(), timeout.count()) << std::endl;
                        timer_running = false;
                    }

                    to_send_num -= (*acked_itor).first - (*acked_itor).second.header_len * 4;
                    ++acked_itor;
                    send_qu.pop_front();
                }

                // timeout
                if (timer_running and std::chrono::steady_clock::now() - st >= timeout)
                {
                    std::cerr << std::format("thread #{}: timeout > {}ms", thread_id, timeout.count()) << std::endl; 
                    auto [send_num, segment] = *acked_itor;
                    while (sendto(this->sock_fd, (char *)&segment, send_num, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to) < 0);
                    timeout *= 2;

                    timeout_trans();
                    ack_counter.clear();
                }
                std::cerr << std::endl;
            }

            return len;
        }

        // user API
        std::map<tcp_struct::seq_t, packet_t> receive_map;
        ssize_t recv(void* buf, size_t len)
        {
            if (not connected) return -1;

            packet_t packet;
            while (true)
            {
                auto it = receive_map.find(this->ack);
                if (it == receive_map.end() or (*it).second.first < 0)
                {
                    std::unique_lock<std::mutex> lock(receive_qu_mutex);
                    receive_qu_cv.wait(lock, [&] { return not receive_qu.empty(); });

                    packet = receive_qu.front(); receive_qu.pop_front();

                    lock.unlock();
                    std::cerr << std::format("thread #{}: Receive packet with seq = {}, ack = {}", 
                            thread_id, (tcp_struct::seq_t)packet.second.seq, (tcp_struct::seq_t)packet.second.ack) << std::endl;
                    std::cerr << std::format("  this->ack = {}", this->ack) << std::endl;
                    receive_map[(tcp_struct::seq_t)packet.second.seq] = packet;
                }

                it = receive_map.find(this->ack);
                if (it == receive_map.end() or (*it).second.first < 0)
                {
                    std::cerr << std::format("thread #{}: The expected seq number {} is empty or recv_num({}) < 0", 
                            thread_id, this->ack, (*it).second.first) << std::endl;
                    std::cerr << std::format("  send ACK, seq = {}, ack = {}", this->seq, this->ack) << std::endl;

                    tcp_struct::segment segment;
                    load_segment(segment);
                    segment.ACK = true;
                    segment.checksum = tcp_checksum((void *)&segment, segment.header_len * 4);
                    std::this_thread::sleep_for(10ms);
                    send_packet(segment);
                    std::this_thread::yield();
                    continue;
                }

                packet = (*it).second;
                receive_map.erase(it);

                break;
            }

            auto [recv_num, recv_segment] = packet;

            if (recv_segment.FIN)
            {
                connected = false;

                std::cerr << std::format("thread #{}: Receive FIN", thread_id) << std::endl;
                this->ack = recv_segment.seq + 1; // this->ack += 1;
                tcp_struct::segment segment;
                load_segment(segment);
                segment.ACK = true;
                segment.checksum = tcp_checksum((void *)&segment, segment.header_len * 4);
                send_packet(segment);
                std::cerr << std::format("thread #{}: Send ACK,\n\tseq = {}, ack = {}", 
                        thread_id, (tcp_struct::seq_t)segment.seq, (tcp_struct::seq_t)segment.ack) << std::endl;

                close();
                return CLOSE_SIGNAL;
            }

            if (recv_segment.ACK)
                this->acked = std::max(this->acked, recv_segment.ack);
            
            size_t data_len = (size_t)recv_num - (size_t)recv_segment.header_len * 4;
            this->rwnd += data_len, this->ack += data_len;
            if (data_len > 0)
            {
                std::cerr << std::format("thread #{}: Receive segment with {} byte data", thread_id, data_len) << std::endl;

                std::cerr << std::format("  send ACK, seq = {}, ack = {}", this->seq, this->ack) << std::endl;
                std::this_thread::sleep_for(10ms);
                tcp_struct::segment segment;
                load_segment(segment);
                segment.ACK = true;
                segment.checksum = tcp_checksum((void *)&segment, segment.header_len * 4);
                send_packet(segment);

                auto recving_size = std::min(data_len, len);
                memcpy((char *)buf, recv_segment.data, recving_size);
            }

            return std::min(data_len, len);
        }

        int close()
        {
            static tcp_struct::segment segment;
            load_segment(segment);
            segment.ACK = true;
            segment.FIN = true;
            segment.checksum = tcp_checksum((void *)&segment, segment.header_len * 4);
            //std::cerr << "the segment sent:\n" << segment << std::endl;
            send_packet(segment);
            std::cerr << std::format("thread #{}: Send FIN, seq = {}, ack = {}", thread_id, this->seq, this->ack) << std::endl;
            ++this->seq;

            std::cerr << std::format("thread #{}: Wait for ACK", thread_id) << std::endl;
            recv_packet(segment);
            if (not (segment.ACK and not corrupt(segment)))
                return -1;
            this->ack = segment.seq + 1;
            std::cerr << std::format("thread #{}: Received ACK", thread_id) << std::endl;

            if (connected)
            {
                std::cerr << std::format("thread #{}: Wait for FIN", thread_id) << std::endl;
                recv_packet(segment);
                if (not (segment.FIN and not corrupt(segment)))
                    return -1;
                load_segment(segment);
                segment.ACK = true;
                send_packet(segment);
                std::cerr << std::format("thread #{}: Send ACK, seq = {}, ack = {}", thread_id, this->seq, this->ack) << std::endl;
                connected = false;
                return 0;
            }
            return -1;
        }
    };
}
#endif
