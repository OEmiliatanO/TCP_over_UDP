#include <iostream>
#include <filesystem>
#include <format>
#include <string>
#include <vector>
#include <thread>
#include <ranges>
#include <fstream>

#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <tcp.h>
#include <utili.h>

using std::cout, std::cerr, std::cin, std::endl, std::format;

constexpr uint16_t listen_port = 9230;
constexpr size_t buf_size = 20;

void main_server(tcp_manager::manager& manager, tcp_connection::connection& channel, int thread_id)
{
    char buf[buf_size]{};
    char data[buf_size]{};
    cout << format("thread #{}: start to receive data", thread_id) << endl;
    ssize_t recv_num;
    while(~(recv_num = channel.recv((void *)buf, buf_size)))
    {
        if (recv_num == tcp_connection::CLOSE_SIGNAL)
        {
            cout << format("thread #{}: Ready to close.", thread_id) << endl;
            manager.release(thread_id);
            break;
        }
        // dns service
        if (buf[0] == 1)
        {
            cout << format("thread #{}: receive from {}:{}: \"dns {}\"", thread_id, inet_ntoa(channel.addr_to.sin_addr), ntohs(channel.addr_to.sin_port), buf+1) << endl;
            strcpy(data, DNS(buf+1));
            cout << "The IP address is " << data << endl;
            channel.send((void *)data, strlen(data));
        }
        // message transmission
        else if (buf[0] == 2)
        {
            cout << format("thread #{}: receive from {}:{}: \"{}\"", thread_id, inet_ntoa(channel.addr_to.sin_addr), ntohs(channel.addr_to.sin_port), buf+1) << endl;
        }
        // calculate service
        else if (buf[0] == 3)
        {
            std::string expression{buf+1};
            std::vector<std::string> op;
            for (auto word : expression | std::views::split(' '))
                op.emplace_back(std::string_view(word));

            double res = 0;
            if (op[0] == "+")
            {
                double a = stod(op[1]), b = stod(op[2]);
                res = a + b;
            }
            else if (op[0] == "-")
            {
                double a = stod(op[1]), b = stod(op[2]);
                res = a - b;
            }
            else if (op[0] == "*")
            {
                double a = stod(op[1]), b = stod(op[2]);
                res = a * b;
            }
            else if (op[0] == "/")
            {
                double a = stod(op[1]), b = stod(op[2]);
                res = a / b;
            }
            else if (op[0] == "^")
            {
                double a = stod(op[1]), b = stod(op[2]);
                res = pow(a, b);
            }
            else if (op[0] == "sqrt")
            {
                double a = stod(op[1]);
                res = sqrt(a);
            }

            strcpy(data, std::to_string(res).c_str());
            channel.send((void *)data, strlen(data));
        }
        // transmission file service
        else if (buf[0] == 4)
        {
            std::string s_buf{buf+1};
            std::vector<std::string> tmp_str;
            for (auto word : s_buf | std::views::split(' '))
                tmp_str.emplace_back(std::string_view(word));

            std::fstream fs;
            std::string file = tmp_str[0];
            fs.open(file, std::ios::binary | std::ios::out);

            constexpr size_t chunk_size = 10000;
            char buffer[chunk_size];

            size_t file_size = std::stoi(tmp_str[1]);
            auto remaining_file_size = file_size;

            while (remaining_file_size)
            {
                ssize_t recv_num = channel.recv((void *)buffer, std::min(remaining_file_size, chunk_size));
                if (recv_num < 0)
                {
                    cerr << "Receiving error: recv_num = " << recv_num << endl;
                    break;
                }
                remaining_file_size -= (size_t)recv_num;
                cout << std::format("Received {:.2f} % of file", 100.0*(file_size-remaining_file_size)/file_size) << endl;
                fs.write(buffer, (size_t)recv_num);
            }
        }
        // require file service
        else if (buf[0] == 5)
        {
            std::fstream fs;
            std::string file{buf+1};
            fs.open(file, std::ios::binary | std::ios::in);

            constexpr size_t chunk_size = 10000;
            char buffer[chunk_size];
            
            auto file_size = std::filesystem::file_size(std::filesystem::path{file});
            auto remaining_file_size = file_size;
            strcpy(data, std::to_string(file_size).c_str());
            
            cout << format("thread #{}: File size = {} bytes", thread_id, file_size) << endl;
            channel.send((void *)data, strlen(data));
            while (remaining_file_size)
            {
                auto send_num = std::min(chunk_size, remaining_file_size);
                fs.read(buffer, send_num);
                channel.send(buffer, send_num);
                remaining_file_size -= send_num;
                std::cout << std::format("thread #{}: Sent {:.2f} % of file", thread_id, 100.0*(file_size-remaining_file_size)/file_size) << std::endl;
            }
        }
        else
        {
            //cout << format("thread #{}: Unknown OPcode from {}:{}: {}", thread_id, inet_ntoa(channel.addr_to.sin_addr), ntohs(channel.addr_to.sin_port), (int)buf[0]) << endl;
        }
        memset(buf, 0, sizeof(buf));
        memset(data, 0, sizeof(data));
    }

    cout << format("thread #{}: close connection.", thread_id) << endl;
    cout << format("Current number of online clients: {}", manager.client_num) << endl;
}

int main([[maybe_unused]]int argc, [[maybe_unused]]char *argv[])
{
    tcp_manager::manager manager(listen_port);
    
    cout << "Listen in port #" << listen_port << endl;
    manager.listen();

    std::vector<std::thread> tpool;
    for (int id; (id = manager.accept()); )
    {
        if (id < 0) { cout << "Connect failed." << endl; continue; }
        tpool.emplace_back(main_server, std::ref(manager), std::ref(manager.connections[id]), id);
        tpool.back().detach();
    }

    return 0;
}
