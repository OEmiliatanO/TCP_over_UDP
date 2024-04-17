#include <unistd.h>
#include <filesystem>
#include <format>
#include <string>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>

#include <tcp.h>

using namespace std::chrono_literals;

using std::cout, std::cerr, std::cin, std::endl, std::format;

constexpr uint16_t server_port = 9230;
const char * host = "127.0.0.1";

constexpr char DNS_OP = 1;

void dns_op(tcp_connection::connection& channel)
{
    char message[20]{};
    char respond[20]{};
    message[0] = 1;
    std::string domain = "www.google.com";
    strcpy(message+1, domain.c_str());
    channel.send((void *)message, (size_t)1+domain.size());
    channel.recv((void *)respond, 20);
    cout << std::format("The IP address of {} is {}", domain, respond) << endl;
}

void send_op(tcp_connection::connection& channel)
{
    char message[20]{};
    std::string sending_string = "hello";
    strcpy(message+1, sending_string.c_str());
    channel.send((void *)message, (size_t)1+sending_string.size());
}

void cal_op(tcp_connection::connection& channel)
{
    char message[20]{};
    char respond[20]{};
    message[0] = 3;
    std::string sending_string = "+ 1 3";
    strcpy(message+1, sending_string.c_str());
    channel.send((void *)message, (size_t)1+sending_string.size());
    channel.recv((void *)respond, 20);
    cout << "The result is " << respond << endl;
}

void trans_op(tcp_connection::connection& channel)
{
    char message[20]{};

    std::string tfile, saving_file;
    tfile = "test.png", saving_file = "test4.png";

    std::fstream fs;
    fs.open(tfile, std::ios::binary | std::ios::in);

    constexpr size_t chunk_size = 10000;
    char buffer[chunk_size];
    
    auto file_size = std::filesystem::file_size(std::filesystem::path{tfile});
    auto remaining_file_size = file_size;
    auto sending_data = saving_file + " " + std::to_string(file_size);
    message[0] = 4;
    strcpy(message+1, sending_data.c_str());
    
    cout << format("File size = {} bytes", file_size) << endl;
    channel.send((void *)message, strlen(message));
    while (remaining_file_size)
    {
        auto send_num = std::min(chunk_size, remaining_file_size);
        fs.read(buffer, send_num);
        channel.send(buffer, send_num);
        remaining_file_size -= send_num;
        cout << format("Send {:.2f} % of file", 100.0*(file_size-remaining_file_size)/file_size) << endl;
    }
    cout << format("Send {} with total {} bytes to server.", tfile, file_size) << endl;
}

void request_op(tcp_connection::connection& channel)
{
    char message[20]{};
    char respond[20]{};

    std::string rfile, saving_file;
    rfile = "test.png", saving_file = "test4.png";

    std::fstream fs;
    fs.open(saving_file, std::ios::binary | std::ios::out);
    
    constexpr size_t chunk_size = 10000;
    char buffer[chunk_size];
    message[0] = 5;
    strcpy(message+1, rfile.c_str());
    channel.send((void *)message, (size_t)1+rfile.size());
    channel.recv((void *)respond, 20);
    
    auto file_size = (size_t)atoi(respond);
    cout << std::format("File size = {} bytes", file_size) << endl;
    auto remaining_file_size = file_size;
    while (remaining_file_size)
    {
        ssize_t recv_num = channel.recv((void *)buffer, std::min(remaining_file_size, chunk_size));
        if (recv_num < 0)
        {
            cout << "Receiving error: recv_num = " << recv_num << endl;
            break;
        }
        remaining_file_size -= (size_t)recv_num;
        cout << format("Received {:.2f} % of file", 100.0*(file_size-remaining_file_size)/file_size) << endl;
        fs.write(buffer, (size_t)recv_num);
    }
    cout << format("Write totally {} bytes into {}", file_size, saving_file) << endl;
}

int client()
{
    tcp_manager::manager client(host, server_port);

    cout << format("connect to {}:{}", host, server_port) << endl;
    int id = -1;
    while ((id = client.connect()) < 0);
    auto& channel = client.connections[id];

    dns_op(channel);
    send_op(channel);
    send_op(channel);
    send_op(channel);
    send_op(channel);
    cal_op(channel);
    trans_op(channel);
    request_op(channel);

    cout << "Going to close the connection" << endl;
    channel.close();
    client.release(id);

    std::cout << "Close connection." << std::endl;

    return 0;
}

int main()
{
    if (fork())
    {
        std::cerr << "fork to server" << std::endl;
        execl("./server.elf", "server.elf", NULL);
        std::cerr << "fork error" << std::endl;
    }
    else
    {
        std::cerr << "fork to client" << std::endl;
        std::this_thread::sleep_for(100ms);
        client();
    }
    return 0;
}
