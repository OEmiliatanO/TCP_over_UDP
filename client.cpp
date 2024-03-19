#include <iostream>
#include <format>
#include <string>
#include <fstream>

#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <tcp.h>

using std::format;

constexpr uint16_t server_port = 9230;
const char * host = "127.0.0.1";

constexpr char DNS_OP = 1;

int main([[maybe_unused]]int argc, [[maybe_unused]]char *argv[])
{
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0)
    {
        std::cerr << "socket error" << std::endl;
        return 1;
    }

    struct sockaddr_in addr_serv;
    memset(&addr_serv, 0, sizeof(struct sockaddr_in));
    addr_serv.sin_family = AF_INET;
    addr_serv.sin_port = htons(server_port);
    addr_serv.sin_addr.s_addr = inet_addr(host);

    tcp_manager client(sock_fd);

    std::cout << format("connect to {}:{}", host, server_port) << std::endl;
    int id = -1;
    while ((id = client.connect(addr_serv)) < 0);
    auto& channel = client.connections[id];

    std::cout << format("OP:\n\tdns <host>\n\tsend <string>\n\tcal <num> <op> <num>\n\ttrans <file path>\n\trequest <required file> <file to store>\n\tclose") << std::endl;
    while (true)
    {
        char message[20]{};
        char respond[20]{};
        std::string input;
        std::cin >> input;
        std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c){ return std::tolower(c); });
        size_t begin_pos = input.length();
        if ((begin_pos = input.find("dns")) != std::string::npos)
        {
            std::string send_string;
            std::cin >> send_string;
            message[0] = 1;
            strcpy(message+1, send_string.c_str());
            channel.send((void *)message, (size_t)1+send_string.size());
            channel.recv((void *)respond, 20);
            std::cout << std::format("The IP address of {} is {}", input, respond) << std::endl;
        }
        else if((begin_pos = input.find("send")) != std::string::npos)
        {
            std::string send_string;
            std::cin >> send_string;
            message[0] = 2;
            strcpy(message+1, send_string.c_str());
            channel.send((void *)message, (size_t)1+send_string.size());
        }
        else if((begin_pos = input.find("cal")) != std::string::npos)
        {
        }
        else if((begin_pos = input.find("transmit")) != std::string::npos)
        {
        }
        else if((begin_pos = input.find("request")) != std::string::npos)
        {
            std::string rfile, saving_file;
            std::cin >> rfile >> saving_file;

            std::fstream fs;
            fs.open(saving_file, std::ios::binary | std::ios::out);
            
            constexpr size_t chunk_size = 10000;
            char buffer[chunk_size];
            message[0] = 5;
            strcpy(message+1, rfile.c_str());
            channel.send((void *)message, (size_t)1+rfile.size());
            channel.recv((void *)respond, 20);
            
            auto file_size = (size_t)atoi(respond);
            std::cout << std::format("File size = {} bytes", file_size) << std::endl;
            auto remaining_file_size = file_size;
            while (remaining_file_size)
            {
                ssize_t recv_num = channel.recv((void *)buffer, std::min(remaining_file_size, chunk_size));
                if (recv_num < 0)
                {
                    std::cerr << "Receiving error: recv_num = " << recv_num << std::endl;
                    break;
                }
                remaining_file_size -= (size_t)recv_num;
                std::cout << std::format("Received {:.2f} % of file", 100.0*(file_size-remaining_file_size)/file_size) << std::endl;
                fs.write(buffer, (size_t)recv_num);
            }
        }
        else if((begin_pos = input.find("close")) != std::string::npos)
        {
            std::cout << "Going to close the connection" << std::endl;
            channel.close();
            client.release(id);
            break;
        }
    }

    std::cout << "Close connection." << std::endl;

    return 0;
}
