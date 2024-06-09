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
    constexpr int FAST_RECOVERY = 3;

    struct connection
    {
        int thread_id;

        int sock_fd;
        tcp_struct::seq_t seq, ack;
        tcp_struct::seq_t acked;
        size_t len_addr_from, len_addr_to;
        sockaddr_in addr_from, addr_to;

        size_t this_rwnd, other_rwnd, cwnd;
        uint8_t this_window_scale;
        uint8_t other_window_scale;
        bool connected;
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
            this->cwnd = MSS;
            this->connected = false;
            this->ssthresh = threshold;
            this->congestion_state = SLOW_START;
            this->acked = 0;
            this->this_rwnd = buffer_size;
            this->other_rwnd = 0;
            this->this_window_scale = window_scale;
            this->other_window_scale = 0;
        }

        void load_segment(tcp_struct::segment& segment)
        {
            segment.clear();
            segment.dst_port = ntohs(this->addr_to.sin_port);
            segment.src_port = ntohs(this->addr_from.sin_port);
            segment.seq = this->seq;
            segment.ack = this->ack;
            segment.header_len = this->header_len / 4;
            segment.window = this->this_rwnd >> this->this_window_scale;
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
            // packet lost with 1e-6 probability
            if (get_random() <= 1)
            {
                std::cerr << std::format("thread #{}: lose packet (SEQ = {}, ACK = {})", 
                        thread_id, (tcp_struct::seq_t)segment.seq, (tcp_struct::seq_t)segment.ack) << std::endl;
                return segment.header_len * 4;
            }

            ssize_t send_num;
            while ((send_num = sendto(this->sock_fd, (char *)&segment, segment.header_len * 4, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to)) < 0);
                //std::cerr << std::format("thread #{}: sendto() errno: ", thread_id) << errno << std::endl;
            std::cerr << std::format("thread #{}: send packet (SEQ = {}, ACK = {})", 
                    thread_id, (tcp_struct::seq_t)segment.seq, (tcp_struct::seq_t)segment.ack) << std::endl;
            return send_num;
        }

        ssize_t send_packet(tcp_struct::segment& segment, size_t send_num)
        {
            // packet lost with 1e-6 probability
            if (get_random() <= 1)
            {
                std::cerr << std::format("thread #{}: lose packet (SEQ = {}, ACK = {})", 
                        thread_id, (tcp_struct::seq_t)segment.seq, (tcp_struct::seq_t)segment.ack) << std::endl;
                return segment.header_len * 4 + send_num;
            }

            ssize_t sent_num;
            while ((sent_num = sendto(this->sock_fd, (char *)&segment, (size_t)segment.header_len * 4 + send_num, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to)) < 0);
                //std::cerr << std::format("thread #{}: In send(), sendto() errno: ", thread_id) << errno << std::endl;
            std::cerr << std::format("thread #{}: send packet (SEQ = {}, ACK = {})", 
                    thread_id, (tcp_struct::seq_t)segment.seq, (tcp_struct::seq_t)segment.ack) << std::endl;
            return sent_num;
        }

        ssize_t send_packet_opt(tcp_struct::segment& segment, size_t opt_size)
        {
            // packet lost with 1e-6 probability
            if (get_random() <= 1)
            {
                std::cerr << std::format("thread #{}: lose packet (SEQ = {}, ACK = {})", 
                        thread_id, (tcp_struct::seq_t)segment.seq, (tcp_struct::seq_t)segment.ack) << std::endl;
                return segment.header_len * 4 + opt_size;
            }

            ssize_t send_num;
            while ((send_num = sendto(this->sock_fd, (char *)&segment, segment.header_len * 4 + opt_size, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to)) < 0);
                //std::cerr << std::format("thread #{}: sendto() errno: ", thread_id) << errno << std::endl;
            std::cerr << std::format("thread #{}: send packet: (SEQ = {}, ACK = {})", 
                    thread_id, (tcp_struct::seq_t)segment.seq, (tcp_struct::seq_t)segment.ack) << std::endl;
            return send_num;
        }

        packet_t retrieve_packet()
        {
            std::unique_lock<std::mutex> lock(receive_qu_mutex);
            receive_qu_cv.wait(lock, [&] { return not receive_qu.empty(); });
            auto packet = receive_qu.front(); receive_qu.pop_front();
            lock.unlock();

            return packet;
        }

        bool has_packet()
        {
            return not receive_qu.empty();
        }

        // the value of this->ack doesn't change when receiving a packet contains no data
        ssize_t recv_packet(tcp_struct::segment& segment)
        {
            auto packet = retrieve_packet();
            auto [recv_num, recv_segment] = packet;
            memcpy(&segment, &recv_segment, sizeof(tcp_struct::segment));
            return recv_num;
        }

        const std::string_view congestion_state_table[4]{"", "slow start", "congestion avoidance","fast recovery"};
        void timeout_trans()
        {
            std::cerr << std::format("thread #{}: Timeout: {} -> slow start", 
                    thread_id, congestion_state_table[congestion_state]) << std::endl;
            this->congestion_state = SLOW_START;
            this->ssthresh = std::max(this->cwnd >> 1, MSS);
            this->cwnd = MSS;
            std::cerr << std::format("            cwnd = {}, rwnd = {}, threshold = {}", 
                    this->cwnd, this->other_rwnd, this->ssthresh) << std::endl;
        }

        void newACK_trans()
        {
            std::cerr << std::format("thread #{}: New ACK: {} -> ", 
                    thread_id, congestion_state_table[congestion_state]);
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
            std::cerr << std::format("{}", congestion_state_table[congestion_state]) << std::endl;
            std::cerr << std::format("            cwnd = {}, rwnd = {}, threshold = {}", 
                    this->cwnd, this->other_rwnd, this->ssthresh) << std::endl;
        }

        void dup3ACK_trans()
        {
            std::cerr << std::format("thread #{}: Dup 3 ACKs: {} -> fast recovery", 
                    thread_id, congestion_state_table[congestion_state]) << std::endl;
            if (this->congestion_state != FAST_RECOVERY)
            {
                this->congestion_state = FAST_RECOVERY;
                this->ssthresh = std::max(this->cwnd >> 1, MSS);
                this->cwnd = this->ssthresh + 3 * MSS;
            }
            std::cerr << std::format("            cwnd = {}, rwnd = {}, threshold = {}", 
                    this->cwnd, this->other_rwnd, this->ssthresh) << std::endl;
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
                segment.checksum = tcp_checksum((void *)&segment, segment.header_len * 4 + sending_size);
                
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
            tcp_struct::seq_t now_measure = std::numeric_limits<tcp_struct::seq_t>::max();
            auto timeout = 1000.0ms, estimate_RTT = 30.0ms, dev_RTT = 0.0ms;
            std::map<tcp_struct::seq_t, size_t> ack_counter;
            //std::cerr << std::format("\nIn send(), start transmit data with {} bytes", len) << std::endl;
            std::cerr << std::format("thread #{}: transmit data with {} bytes", thread_id, len) << std::endl;
            while (not send_qu.empty())
            {
                // send segments
                //std::cerr << std::format("thread #{}: expect {} bytes in receiver's buffer", thread_id, to_send_num) << std::endl;
                //std::cerr << std::format("            {} packets remain in sending queue", send_qu.size()) << std::endl;
                //std::cerr << std::format("  packet next is {} bytes", (*sent_itor).first - (*sent_itor).second.header_len * 4) << std::endl;
                while (sent_itor != send_qu.end() 
                        and (to_send_num + (*sent_itor).first - (*sent_itor).second.header_len * 4) <= std::min(other_rwnd, cwnd))
                {
                    to_send_num += (*sent_itor).first - (*sent_itor).second.header_len * 4;
                    total_send += (*sent_itor).first - (*sent_itor).second.header_len * 4;
                    
                    //std::cerr << std::format("thread #{}: current total send byte: {}", thread_id, total_send) << std::endl;
                    //std::cerr << std::format("  seq = {}, ack = {}", 
                    //        (tcp_struct::seq_t)(*sent_itor).second.seq, (tcp_struct::seq_t)(*sent_itor).second.ack) << std::endl;
                    //std::cerr << std::format("  congestion state = {}, cwnd = {}, other's rwnd = {}, threshold = {}", 
                    //       congestion_state_table[congestion_state], cwnd, other_rwnd, ssthresh) << std::endl;

                    auto [send_num, segment] = *sent_itor++;

                    if (not timer_running and now_measure == std::numeric_limits<tcp_struct::seq_t>::max())
                    {
                        now_measure = segment.seq;
                        timer_running = true;
                        st = std::chrono::steady_clock::now();
                    }

                    //std::cerr << std::format("thread #{}: send {}", thread_id, send_num - (size_t)segment.header_len * 4) << std::endl;
                    send_packet(segment, send_num - (size_t)segment.header_len * 4);
                    
                    /*
                    std::this_thread::sleep_for(10ms);
                    while (sendto(this->sock_fd, (char *)&segment, send_num, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to) < 0)
                        std::cerr << std::format("thread #{}: In send(), sendto() errno: ", thread_id) << errno << std::endl;
                    */
                }

                // receive ACKs
                //std::cerr << std::format("thread #{}: Wait for ACK, seq = {}", thread_id, (tcp_struct::seq_t)(*acked_itor).second.seq) << std::endl;
                tcp_struct::segment recv_segment;

                // timeout mechanism
                while (true)
                {
                    if (not timer_running)
                    {
                        timer_running = true;
                        st = std::chrono::steady_clock::now();
                    }
                    if (not this->has_packet() and timer_running and std::chrono::steady_clock::now() - st >= timeout)
                    {
                        std::cerr << std::format("thread #{}: timeout > {}ms", thread_id, timeout.count()) << std::endl; 
                        auto [send_num, segment] = *acked_itor;
                        send_packet(segment, send_num - (size_t)segment.header_len * 4);
                        //while (sendto(this->sock_fd, (char *)&segment, send_num, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to) < 0);
                        timeout *= 2;
                        st = std::chrono::steady_clock::now();

                        timeout_trans();
                        ack_counter.clear();
                    }
                    if (this->has_packet()) break;
                }

                recv_packet(recv_segment);
                tcp_struct::seq_t recv_ack = recv_segment.ack;

                //std::cerr << std::format("thread #{}: Receive segment, ACK enable = {}, seq = {}, ack = {}", 
                //        thread_id, (bool)recv_segment.ACK, (tcp_struct::seq_t)recv_segment.seq, (tcp_struct::seq_t)recv_segment.ack) << std::endl;
                //std::cerr << std::format("  this->acked = {}, ack_counter[recv_ack] = {}", this->acked, ack_counter[recv_ack]) << std::endl;
                std::cerr << std::format("thread #{}: receive ACK (SEQ = {}, ACK = {})", 
                        thread_id, (tcp_struct::seq_t)recv_segment.seq, (tcp_struct::seq_t)recv_segment.ack) << std::endl;

                ++ack_counter[recv_ack];
                if (ack_counter[recv_ack] >= 2 and congestion_state == FAST_RECOVERY)
                    cwnd += MSS;

                // fast retransmit
                if (ack_counter[recv_ack] >= 3)
                {
                    std::cerr << std::format("thread #{}: Receive 3 duplicate ACKs, ack = {}, retransmit packet with SEQ = {}, ACK = {}", 
                            thread_id, recv_ack, (tcp_struct::seq_t)(*acked_itor).second.seq, (tcp_struct::seq_t)(*acked_itor).second.ack) << std::endl;
                    auto [send_num, segment] = *acked_itor;
                    send_packet(segment, send_num - (size_t)segment.header_len * 4);
                    //while (sendto(this->sock_fd, (char *)&segment, send_num, 0, (sockaddr *)&this->addr_to, (socklen_t)this->len_addr_to) < 0);
                    ack_counter.erase(recv_ack);
                    
                    dup3ACK_trans();
                    if (timer_running)
                        st = std::chrono::steady_clock::now();
                }

                // new ACK
                if (recv_segment.ACK and ack_counter[recv_ack] <= 1 and recv_segment.ack > this->acked)
                {
                    timeout = estimate_RTT + 4 * dev_RTT;
                    this->acked = recv_segment.ack;
                    this->other_rwnd = static_cast<size_t>(recv_segment.window) << this->other_window_scale;
                    //std::cerr << std::format("thread #{}: other's rwnd = {}", thread_id, this->other_rwnd) << std::endl;
                    newACK_trans();
                    ack_counter.clear();

                    while (not send_qu.empty() and (*acked_itor).second.seq < this->acked)
                    {
                        //std::cerr << std::format("thread #{}: acked_itor.seq = {} < acked = {}, pop out.", 
                        //        thread_id, (tcp_struct::seq_t)(*acked_itor).second.seq, this->acked) << std::endl;

                        to_send_num -= (*acked_itor).first - (*acked_itor).second.header_len * 4;
                        ++acked_itor;
                        send_qu.pop_front();
                    }

                    if (timer_running and recv_segment.ack > now_measure)
                    {
                        auto sample_RTT = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - st);
                        estimate_RTT = (1-alpha) * estimate_RTT + alpha * sample_RTT;
                        dev_RTT = (1-beta) * dev_RTT + beta * abs(sample_RTT - estimate_RTT);
                        timeout = estimate_RTT + 4 * dev_RTT;
                        //std::cerr << std::format("thread #{}: sample RTT = {}, \n\testimate RTT = {}, dev RTT = {}, timeout interval = {}", 
                        //        thread_id, sample_RTT.count(), estimate_RTT.count(), dev_RTT.count(), timeout.count()) << std::endl;
                        timer_running = false;
                    }
                    
                    if (std::distance(acked_itor, sent_itor) > 0)
                    {
                        timer_running = true;
                        st = std::chrono::steady_clock::now();
                        now_measure = std::numeric_limits<tcp_struct::seq_t>::max();
                    }
                    else
                        timer_running = false;
                }
                
            }

            return len;
        }
        

        void sendACK()
        {
            tcp_struct::segment segment;
            load_segment(segment);
            segment.ACK = true;
            segment.checksum = tcp_checksum((void *)&segment, segment.header_len * 4);
            std::cerr << std::format("thread #{}: send ACK", thread_id) << std::endl;
            send_packet(segment);
        }

        std::chrono::milliseconds delay_duration = 500ms;
        std::map<tcp_struct::seq_t, packet_t> receive_map;
        tcp_struct::seq_t max_recv_seq = 0;
        bool detect_gap = false;
        ssize_t recv(void* buf, size_t len)
        {
            packet_t swp_packet;
            size_t packet_cnt = 0;

            while (true)
            {
                auto it = receive_map.find(this->ack);
                if (it == receive_map.end() or (*it).second.first < 0)
                {
RETRIEVE_PACKET:
                    auto packet = retrieve_packet();
                    auto [recv_num, recv_segment] = packet;
                    auto seq_num = recv_segment.seq;
                    max_recv_seq = std::max(seq_num, max_recv_seq);
                    receive_map[seq_num] = packet;
                    //std::cerr << std::format("thread #{}: this->ack = {}, seq_num = {}", thread_id, this->ack, seq_num) << std::endl;
                    std::cerr << std::format("thread #{}: receive packet (SEQ = {}, ACK = {})", 
                            thread_id, seq_num, (tcp_struct::seq_t)recv_segment.ack) << std::endl;

                    // receive out-of-order segment, detect gap
                    if (not detect_gap and this->ack < seq_num)
                    {
                        std::cerr << std::format("thread #{}: expect SEQ = {}", thread_id, this->ack) << std::endl;
                        std::cerr << std::format("            but receive SEQ = {}", seq_num) << std::endl;
                        std::cerr << std::format("            send duplicate ACKs") << std::endl;
                        detect_gap = true;

                        for (size_t i = 0; i < 3; ++i)
                            sendACK();

                        if (packet_cnt)
                        {
                            auto [swp_recv_num, swp_recv_segment] = swp_packet;
                            auto swp_data_size = static_cast<size_t>(swp_recv_num - swp_recv_segment.header_len * 4);
                            this->this_rwnd -= swp_data_size, this->ack -= swp_data_size;
                            packet_cnt = 0;
                        }

                        continue;
                    }
                    // filling the gap
                    else if (detect_gap)
                    {
                        if (this->ack < seq_num)
                        {
                            std::cerr << std::format("thread #{}: filling gap (SEQ = {})", thread_id, seq_num) << std::endl;
                            sendACK();

                            if (packet_cnt)
                            {
                                auto [swp_recv_num, swp_recv_segment] = swp_packet;
                                auto swp_data_size = static_cast<size_t>(swp_recv_num - swp_recv_segment.header_len * 4);
                                this->this_rwnd -= swp_data_size, this->ack -= swp_data_size;
                                packet_cnt = 0;
                            }

                            continue;
                        }
                        else if (this->ack == seq_num)
                        {
                            size_t data_size = recv_num - recv_segment.header_len * 4;
                            this->this_rwnd += data_size, this->ack += data_size;
                            sendACK();
                            this->this_rwnd -= data_size, this->ack -= data_size;
                            continue;
                        }
                    }

                    // receive in-order segment
                    if (not detect_gap and this->ack == seq_num)
                    {
                        //std::cerr << std::format("thread #{}: receive packet (SEQ = {}, ACK = {})", 
                        //        thread_id, seq_num, (tcp_struct::seq_t)recv_segment.ack) << std::endl;
                        ++packet_cnt;

                        size_t data_size = recv_num - recv_segment.header_len * 4;
                        this->this_rwnd += data_size, this->ack += data_size;

                        if (packet_cnt == 1)
                        {
                            /*
                            std::cerr << std::format("  Wait 500ms for next segment") << std::endl;
                            constexpr size_t polling_num = 10;
                            for (size_t i = 0; i < polling_num; ++i)
                            {
                                std::this_thread::sleep_for(delay_duration / polling_num);
                                if (not receive_qu.empty()) break;
                            }
                            */
                            if (not receive_qu.empty())
                            {
                                //std::cerr << std::format("  Next segment arrived, retrieve the segment") << std::endl;
                                swp_packet = packet;
                                goto RETRIEVE_PACKET;
                            }
                            else
                            {
                                //std::cerr << std::format("  Next segment doesn't arrive, retrieve the segment") << std::endl;
                                if (recv_segment.FIN)
                                {
                                    close_state(recv_segment);
                                    return CLOSE_SIGNAL;
                                }

                                if (recv_segment.ACK)
                                    this->acked = std::max(this->acked, recv_segment.ack);

                                sendACK();

                                //std::cerr << std::format("thread #{}: Receive segment with {} byte data", thread_id, data_size) << std::endl;
                                //std::cerr << std::format("  send ACK, seq = {}, ack = {}", this->seq, this->ack) << std::endl;

                                auto recving_size = std::min(data_size, len);
                                memcpy((char *)buf, recv_segment.data, recving_size);

                                return recving_size;
                            }
                        }
                        else if (packet_cnt == 2)
                        {
                            sendACK();

                            packet_cnt = 0;

                            //std::cerr << std::format("thread #{}: Receive consecutive segment with {} byte data", thread_id, data_size) << std::endl;
                            //std::cerr << std::format("  send ACK, seq = {}, ack = {}", this->seq, this->ack) << std::endl;

                            this->this_rwnd -= data_size, this->ack -= data_size; // in order to retrieve next packet

                            auto [swp_recv_num, swp_recv_segment] = swp_packet;
                            
                            if (swp_recv_segment.ACK)
                                this->acked = std::max(this->acked, swp_recv_segment.ack);

                            auto swp_data_size = static_cast<size_t>(swp_recv_num - swp_recv_segment.header_len * 4);

                            auto recving_size = std::min(swp_data_size, len);
                            memcpy((char *)buf, swp_recv_segment.data, recving_size);

                            return recving_size;
                        }
                    }

                    if (packet_cnt)
                    {
                        auto [swp_recv_num, swp_recv_segment] = swp_packet;

                        if (swp_recv_segment.ACK)
                            this->acked = std::max(this->acked, swp_recv_segment.ack);
                        
                        auto swp_data_size = static_cast<size_t>(swp_recv_num - swp_recv_segment.header_len * 4);
                        sendACK();
                        
                        //std::cerr << std::format("thread #{}: receive packet and this->ack = {} > seq_num = {} and packet_cnt = {}", 
                        //        thread_id, this->ack, seq_num, packet_cnt) << std::endl;
                        //std::cerr << std::format("  send ACK, ack = {}", this->ack) << std::endl;
                        
                        packet_cnt = 0;

                        auto recving_size = std::min(swp_data_size, len);
                        memcpy((char *)buf, swp_recv_segment.data, recving_size);

                        return recving_size;
                    }
                    
                    if (this->ack > seq_num)
                    {
                        sendACK();
                        continue;
                    }
                }
                // find seq=this->ack in receive_map
                else
                {
                    auto [recv_num, recv_segment] = (*it).second;

                    if (recv_segment.FIN)
                    {
                        close_state(recv_segment);
                        return CLOSE_SIGNAL;
                    }

                    if (recv_segment.ACK)
                        this->acked = std::max(this->acked, recv_segment.ack);

                    size_t data_size = recv_num - recv_segment.header_len * 4;
                    this->this_rwnd += data_size, this->ack += data_size;

                    // gap eliminated
                    //std::cerr << std::format("thread #{}: detect_gap = {}, this->ack = {}, max_recv_seq = {}", 
                    //        thread_id, detect_gap, this->ack, max_recv_seq) << std::endl;
                    if (detect_gap and this->ack > max_recv_seq)
                    {
                        detect_gap = false;
                        //std::cerr << std::format("thread #{}: this->ack = {} > max_recv_seq = {}, gap eliminated", 
                        //        thread_id, this->ack, max_recv_seq) << std::endl;

                        sendACK();
                        //std::cerr << std::format("thread #{}: Send ACK,\n\tseq = {}, ack = {}", 
                        //        thread_id, this->seq, this->ack) << std::endl;
                    }

                    //std::cerr << std::format("thread #{}: Retrieve segment with {} byte data", thread_id, data_size) << std::endl;

                    auto recving_size = std::min(data_size, len);
                    memcpy((char *)buf, recv_segment.data, recving_size);

                    return recving_size;
                }
            }
        }

        void close_state(tcp_struct::segment& recv_segment)
        {
            tcp_struct::segment segment;
            auto sendFIN = [&]() {
                load_segment(segment);
                segment.ACK = true;
                segment.FIN = true;
                segment.checksum = tcp_checksum((void *)&segment, segment.header_len * 4);
                std::cerr << std::format("thread #{}: send FIN", thread_id) << std::endl;
                send_packet(segment);
            };

            connected = false;

            std::cerr << std::format("thread #{}: Receive FIN (SEQ = {}, ACK = {})", 
                    thread_id, (tcp_struct::seq_t)recv_segment.seq, (tcp_struct::seq_t)recv_segment.ack) << std::endl;

            this->ack = recv_segment.seq + 1; // this->ack += 1;
            sendACK();
            
            sendFIN();

            size_t t = 1000, accum_t = 0;
            while (not this->has_packet())
            {
                std::this_thread::sleep_for(10ms);
                accum_t += 10;
                if (accum_t >= t)
                    return;
            }
            if (this->has_packet())
                recv_packet(segment);
        }

        int close() // client
        {
            // CLOSE_WAIT
        
            tcp_struct::segment segment;
            auto sendFIN = [&]() {
                load_segment(segment);
                segment.ACK = true;
                segment.FIN = true;
                segment.checksum = tcp_checksum((void *)&segment, segment.header_len * 4);
                send_packet(segment);
            };

            // clear out the buffer
            while (this->has_packet())
                recv_packet(segment);

            std::cerr << std::format("thread #{}: Send FIN", thread_id) << std::endl;
            sendFIN(); // FIN_WAIT1

            while (true)
            {
                size_t t = 1000, accum_t = 0;
                bool retry = true;
                while (not this->has_packet())
                {
                    std::this_thread::sleep_for(10ms);
                    accum_t += 10;
                    if (accum_t >= t)
                    {
                        accum_t = 0;
                        std::cerr << std::format("thread #{}: Timeout, retransmit FIN segment.", thread_id) << std::endl;
                        sendFIN();
                        if (not retry) break;
                        retry = false;
                    }
                }
                if (not retry and not this->has_packet()) return 0;

                auto recv_num = recv_packet(segment);
                if (recv_num > 0 and segment.ACK and not corrupt(segment, recv_num))
                    break;
            }

            ++this->seq;
            this->ack = segment.seq + 1;

            // FIN_WAIT2
            // successfully recevice the ACK
            std::cerr << std::format("thread #{}: Received ACK (SEQ = {}, ACK = {})", 
                    thread_id, (tcp_struct::seq_t)segment.seq, (tcp_struct::seq_t)segment.ack) << std::endl;

            size_t MSL = 1000, accum_t = 0;
            while (true)
            {
                while (not this->has_packet())
                {
                    std::this_thread::sleep_for(10ms);
                    accum_t += 10;
                    if (accum_t >= 2 * MSL)
                        return 0;
                }
                // TIME_WAIT
                auto recv_num = recv_packet(segment);
                if (not (recv_num > 0 and segment.FIN and not corrupt(segment, recv_num)))
                    continue;

                this->ack = segment.seq + 1;
                sendACK();
                return 0;
            }
            return 0;
        }
    };
}
#endif
