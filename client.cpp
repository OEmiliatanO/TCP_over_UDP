#include <iostream>
#include <filesystem>
#include <format>
#include <string>
#include <fstream>

#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <tcp.h>

using std::cout, std::cerr, std::cin, std::endl, std::format;

constexpr uint16_t server_port = 9230;
const char * host = "127.0.0.1";

constexpr char DNS_OP = 1;

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
    addr_serv.sin_port = htons(server_port);
    addr_serv.sin_addr.s_addr = inet_addr(host);

    tcp_manager client(sock_fd);

    cout << format("connect to {}:{}", host, server_port) << endl;
    int id = -1;
    while ((id = client.connect(addr_serv)) < 0);
    auto& channel = client.connections[id];

    cout << format("OP:\n\tdns <host>\n\tsend <string>\n\tcal <num> <op> <num>/<num> sqrt\n\ttrans <local file to transmit> <remote file to store>\n\trequest <remote file to transmit> <local file to store>\n\tclose") << endl;
    while (true)
    {
        char message[20]{};
        char respond[20]{};
        std::string input;
        cin >> input;
        std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c){ return std::tolower(c); });
        size_t begin_pos = input.length();
        if ((begin_pos = input.find("dns")) != std::string::npos)
        {
            std::string send_string;
            cin >> send_string;
            message[0] = 1;
            strcpy(message+1, send_string.c_str());
            channel.send((void *)message, (size_t)1+send_string.size());
            channel.recv((void *)respond, 20);
            cout << std::format("The IP address of {} is {}", input, respond) << endl;
        }
        else if((begin_pos = input.find("send")) != std::string::npos)
        {
            std::string send_string;
            cin >> send_string;
            message[0] = 2;
            strcpy(message+1, send_string.c_str());
            channel.send((void *)message, (size_t)1+send_string.size());
        }
        else if((begin_pos = input.find("cal")) != std::string::npos)
        {
            std::string a, b, sending_string;
            cin >> a;
            std::string op;
            cin >> op;
            std::transform(op.begin(), op.end(), op.begin(), [](unsigned char c){ return std::tolower(c); });
            if (op.find("sqrt") == std::string::npos)
                cin >> b;
            
            message[0] = 3;
            sending_string = op + " " + a;
            if (not b.empty())
                sending_string += " " + b;

            strcpy(message+1, sending_string.c_str());
            channel.send((void *)message, (size_t)1+sending_string.size());
            channel.recv((void *)respond, 20);

            cout << "The result is " << respond << endl;
        }
        else if((begin_pos = input.find("trans")) != std::string::npos)
        {
            std::string tfile, saving_file;
            cin >> tfile >> saving_file;

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
                cout << format("Sent {:.2f} % of file", 100.0*(file_size-remaining_file_size)/file_size) << endl;
            }
        }
        else if((begin_pos = input.find("request")) != std::string::npos)
        {
            std::string rfile, saving_file;
            cin >> rfile >> saving_file;

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
                    cerr << "Receiving error: recv_num = " << recv_num << endl;
                    break;
                }
                remaining_file_size -= (size_t)recv_num;
                cout << std::format("Received {:.2f} % of file", 100.0*(file_size-remaining_file_size)/file_size) << endl;
                fs.write(buffer, (size_t)recv_num);
            }
        }
        else if((begin_pos = input.find("close")) != std::string::npos)
        {
            cout << "Going to close the connection" << endl;
            channel.close();
            client.release(id);
            break;
        }
    }

    std::cout << "Close connection." << std::endl;

    return 0;
}
