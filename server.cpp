#include <iostream>
#include <format>
#include <string>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <tcp.h>

using std::format;

constexpr uint16_t listen_port = 9230;

const char* DNS(const char* host)
{
    struct hostent *hent;
    hent = gethostbyname(host);
    char** pptr = hent->h_addr_list;
    char buf[1024]{};
    if (pptr == NULL)
        return nullptr;
    return inet_ntop(hent->h_addrtype, *pptr, buf, sizeof(buf));
}

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
    addr_serv.sin_port = htons(listen_port);
    addr_serv.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(sock_fd, (struct sockaddr *)&addr_serv, sizeof(addr_serv)) < 0)
    {
        std::cerr << "bind error" << std::endl;
        return 1;
    }
    
    ssize_t recv_num = 0;

    tcp_connection welcome_sock;
    welcome_sock.sock_fd = sock_fd;
    welcome_sock.addr_from = addr_serv;
    
    while (true)
    {
        std::cout << format("parent process listen in port {}", listen_port) << std::endl;
        while (welcome_sock.listen());
        pid_t pid = fork();
        if (pid < 0) { std::cerr << "fork error" << std::endl; return -1; }
        if (pid == 0) break;
        continue;
    }

    int client_fd = welcome_sock.accept();

    tcp_connection connection = welcome_sock;
    connection.sock_fd = client_fd;

    char buf[21]{};
    std::cout << "start to recv data" << std::endl;
    while(~(recv_num = connection.recv((void *)buf, 20)))
    {
        std::cout << format("recv from [{}:{}]: \"{}\"", inet_ntoa(connection.addr_to.sin_addr), ntohs(connection.addr_to.sin_port), buf) << std::endl;
    }

    std::cout << "close connection" << std::endl;

    return 0;
}
