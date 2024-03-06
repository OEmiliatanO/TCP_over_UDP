#include <iostream>
#include <format>
#include <string>
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

    tcp_manager client;
    client.sock_fd = sock_fd;

    std::cout << format("connect to {}:{}", host, server_port) << std::endl;
    int id = -1;
    id = client.connect(addr_serv);
    auto& channel = client.connections[id];

    std::cout << format("op:\n\tdns <host>\n\tsend <string>\n\tcal <num> <op> <num>\n\ttrans <file path>\n\trequest\n\tclose") << std::endl;
    while (true)
    {
        char message[20]{};
        char respond[20]{};
        std::string input;
        std::cin >> input;
        std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c){ return std::tolower(c); });
        size_t begin_pos, end_pos = input.length();
        if ((begin_pos = input.find("dns")) != std::string::npos)
        {
            std::string send_string;
            std::cin >> send_string;
            strcpy(message, send_string.c_str());
            client.send(id, (void *)message, (size_t)send_string.size());
            client.recv(id, (void *)respond, 20);
            std::cout << std::format("respond for \"{}\": {}", input, respond) << std::endl;
        }
        else if((begin_pos = input.find("send")) != std::string::npos)
        {
            std::string send_string;
            std::cin >> send_string;
            strcpy(message, send_string.c_str());
            std::cerr << "message = " << message << std::endl;
            client.send(id, (void *)message, (size_t)send_string.size());
        }
        else if((begin_pos = input.find("cal")) != std::string::npos)
        {
        }
        else if((begin_pos = input.find("transmit")) != std::string::npos)
        {
        }
        else if((begin_pos = input.find("request")) != std::string::npos)
        {
        }
        else if((begin_pos = input.find("close")) != std::string::npos)
        {
            client.close(id);
            break;
        }
    }

    std::cout << "close connection" << std::endl;
    while(!(~channel.close()));

    return 0;
}
