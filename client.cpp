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

int main([[maybe_unused]]int argc, [[maybe_unused]]char *argv[])
{
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0)
    {
        std::cerr << "socket error" << std::endl;
        return 1;
    }

    struct sockaddr_in addr_serv;
    [[maybe_unused]]int len = 0;
    memset(&addr_serv, 0, sizeof(struct sockaddr_in));
    addr_serv.sin_family = AF_INET;
    addr_serv.sin_port = htons(server_port);
    addr_serv.sin_addr.s_addr = inet_addr(host);
    len = sizeof(addr_serv);

    tcp_connection channel;
    channel.sock_fd = sock_fd;

    std::cout << format("connect to {}:{}", host, server_port) << std::endl;
    channel.connect(addr_serv);

    char data[20] = "123456";
    std::cout << format("send \"{}\" to {}:{}", data, host, ntohs(addr_serv.sin_port)) << std::endl;
    channel.send((void *)data, 7);

    std::cout << "close connection" << std::endl;
    while(!(~channel.close()));

    return 0;
}
