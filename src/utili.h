#ifndef __UTILI_H__
#define __UTILI_H__

#include <string>
#include <format>
#include <random>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

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

std::string sockaddr_to_string(sockaddr_in& addr)
{
    return std::format("{}:{}", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
}

int gen_id()
{
    static std::random_device r;
    static std::default_random_engine eng(r());
    static std::uniform_int_distribution<int> uniform_dist(0, std::numeric_limits<int>::max());
    return uniform_dist(eng);
}

int get_random()
{
    static std::random_device r;
    static std::default_random_engine eng(r());
    static std::uniform_int_distribution<int> uniform_dist(1, 1e6);
    return uniform_dist(eng);
}

#endif
