#include <iostream>
#include <filesystem>
#include <format>
#include <string>
#include <fstream>
#include <cstdlib>

#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <tcp.h>

using std::cout, std::cerr, std::cin, std::endl, std::format;

//constexpr uint16_t server_port = 9230;
//const char * host = "127.0.0.1";

constexpr char DNS_OP = 1;

// ./client.elf 127.0.0.1 9230 --interact/--oneline
//      --dns dns.google.com 
//      --send "hello" 
//      --cal <num> <op> <num> / --cal <num> sqrt
//      --trans <local file> <path to be stored (remotely)>
//      --request <remote file> <path to be stored (locally)>

std::string dns(std::string host, tcp_connection::connection& channel)
{
    char message[20]{}, respond[20]{};
    message[0] = 1;
    strcpy(message+1, host.c_str());
    channel.send((void *)message, (size_t)1+host.size());
    channel.recv((void *)respond, 20);
    return respond;
}

void send(std::string s, tcp_connection::connection& channel)
{
    char message[20]{};
    message[0] = 2;
    strcpy(message+1, s.c_str());
    channel.send((void *)message, (size_t)1+s.size());
}

std::string cal(std::string op, std::string a, std::string b, tcp_connection::connection& channel)
{
    char message[50]{}, respond[50]{};
    message[0] = 3;
    std::string sending_string = op + " " + a;
    if (not b.empty())
        sending_string += " " + b;

    strcpy(message+1, sending_string.c_str());
    channel.send((void *)message, (size_t)1+sending_string.size());
    channel.recv((void *)respond, 50);
    
    return respond;
}

void trans(std::string tfile, std::string saving_file, tcp_connection::connection& channel)
{
    char message[50]{};
    std::fstream fs;
    fs.open(tfile, std::ios::binary | std::ios::in);

    constexpr size_t chunk_size = 100000; // 100KB
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

void request(std::string rfile, std::string saving_file, tcp_connection::connection& channel)
{
    char message[50]{}, respond[50]{};
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

int main([[maybe_unused]]int argc, [[maybe_unused]]char *argv[])
{
    const char * host = argv[1];
    uint16_t server_port = std::atoi(argv[2]);
    tcp_manager::manager client(host, server_port);

    cout << format("connect to {}:{}", host, server_port) << endl;
    int id = -1;
    while (id < 0)
    {
        cout << "(connecting)" << endl;
        id = client.connect();
        cout << "(connected)" << endl;
    }
    auto& channel = client.connections[id];

    std::string mode = argv[3];
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c){ return std::tolower(c); });
    if (mode == "--oneline")
    {
        size_t order_cnt = 1;
        for (size_t i = 4; i < static_cast<size_t>(argc);)
        {
            std::string order = argv[i];
            std::transform(order.begin(), order.end(), order.begin(), [](unsigned char c){ return std::tolower(c); });
            cout << format("(task {} : {})", order_cnt, order) << endl;
            if (order == "--dns")
            {
                cout << format("The IP address of {} is {}", argv[i+1], dns(argv[i+1], channel)) << endl;
                i += 2;
            }
            else if (order == "--send")
            {
                send(argv[i+1], channel);
                cout << format("Message sent") << endl;
                i += 2;
            }
            else if (order == "--cal")
            {
                std::string op = argv[i+2], a = argv[i+1], b = "";
                std::transform(op.begin(), op.end(), op.begin(), [](unsigned char c){ return std::tolower(c); });
                if (op != "sqrt")
                {
                    b = argv[i+3];
                    i += 4;
                }
                else i += 3;

                cout << format("The result is {}", cal(op, a, b, channel)) << endl;
            }
            else if (order == "--trans")
            {
                trans(argv[i+1], argv[i+2], channel);
                i += 3;
            }
            else if (order == "--request")
            {
                request(argv[i+1], argv[i+2], channel);
                i += 3;
            }
            else
            {
                cout << format("Unknown instruction: {}", argv[i]) << endl;
                ++i;
            }
            cout << format("(task {} completed)", order_cnt++) << endl;
        }
        channel.close();
        client.release(id);
    }
    else if (mode == "--interact")
    {
        cout << format("OP:\n\tdns <host>\n\tsend <string>\n\tcal <num> <op> <num>/<num> sqrt\n\ttrans <local file to transmit> <remote file to store>\n\trequest <remote file to transmit> <local file to store>\n\tclose") << endl;
        while (true)
        {
            std::string order;
            cin >> order;
            std::transform(order.begin(), order.end(), order.begin(), [](unsigned char c){ return std::tolower(c); });

            if (order == "dns")
            {
                std::string host;
                cin >> host;
                cout << format("The IP address of {} is {}", host, dns(host, channel)) << endl;
            }
            else if(order == "send")
            {
                std::string s;
                cin >> s;
                send(s, channel);
            }
            else if(order == "cal")
            {
                std::string a, b, sending_string;
                cin >> a;
                std::string op;
                cin >> op;
                std::transform(op.begin(), op.end(), op.begin(), [](unsigned char c){ return std::tolower(c); });
                if (op.find("sqrt") == std::string::npos)
                    cin >> b;

                cout << format("The result is {}", cal(op, a, b, channel)) << endl;
            }
            else if(order == "trans")
            {
                std::string tfile, saving_file;
                cin >> tfile >> saving_file;

                trans(tfile, saving_file, channel);
            }
            else if(order == "request")
            {
                std::string rfile, saving_file;
                cin >> rfile >> saving_file;

                request(rfile, saving_file, channel);
            }
            else if(order == "close")
            {
                cout << "Going to close the connection" << endl;
                channel.close();
                client.release(id);
                break;
            }
        }

    }

    std::cout << "Close connection." << std::endl;

    return 0;
}
