# TCP-over-UDP

This small project builds TCP based on UDP.

You can find how to use the simple TCP in client.cpp and server.cpp.

**Master branch is implemented based on TCP Reno*

## Compile

Require g++ version at least 13.1.0 and Linux environment.

To compile:
```
make
```

To clean obj files:
```
make clean
```

## Usage

First, 
```
./server <server port>
```

Then, 
```
./client <server IP> <server port> <--oneline/--interact> <instructions>
```

The instructions: 
1. --dns \<hostname\>
2. --send \<string\>
3. --cal \<num1\> \<op\> \<num2\>
4. --trans \<local file\> \<path to be stored (remotely)\>
5. --request \<remote file\> \<path to be stored (locally)\>

## Structure

src/tcp\_struct.h defines the TCP segment structure.  
src/tcp\_para.h defines some TCP parameters like MSS, buffer size, ...  
src/tcp\_utili.h defines some function for TCP, e.g., generate ISN, checksum.  
src/tcp\_mux.h defines TCP multiplexer.  
src/tcp\_connection.h defines the connection of TCP, which maintains the state of the connection and provides APIs (recv, send) for user.  
src/tcp.h defines the manager of TCP connection, which maintains all connetion and dispatchs the receiving data to corresponding connection based on IP addresses and ports.  
