#include <iostream>
#include <format>
#include <string>
#include <vector>
#include <thread>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <tcp.h>
#include <utili.h>

using std::cout, std::cerr, std::cin, std::endl, std::format;

constexpr uint16_t listen_port = 9230;
constexpr size_t buf_size = 20;

void server_func(tcp_connection& channel, int thread_id)
{
    char buf[buf_size]{};
    cout << format("thread #{}: start to recv data", thread_id) << endl;
    ssize_t recv_num;
    while(~(recv_num = channel.recv((void *)buf, buf_size)))
    {
        cout << format("thread #{}: recv from client [{}:{}]: \"{}\"", thread_id, inet_ntoa(channel.addr_to.sin_addr), ntohs(channel.addr_to.sin_port), buf) << endl;
        memset(buf, 0, sizeof(buf));
    }

    cout << format("thread #{}: close connection", thread_id) << endl;

}

int main([[maybe_unused]]int argc, [[maybe_unused]]char *argv[])
{
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0)
    {
        cerr << "socket error" << endl;
        return 1;
    }

    struct sockaddr_in addr_serv;
    memset(&addr_serv, 0, sizeof(struct sockaddr_in));
    addr_serv.sin_family = AF_INET;
    addr_serv.sin_port = htons(listen_port);
    addr_serv.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(sock_fd, (struct sockaddr *)&addr_serv, sizeof(addr_serv)) < 0)
    {
        cerr << "bind error" << endl;
        return 1;
    }
    
    tcp_manager server;
    server.sock_fd = sock_fd;
    server.addr_from = addr_serv;
    server.server_func = server_func;
    
    std::cout << "listen in port " << listen_port << std::endl;
    server.listen();

    return 0;
}
