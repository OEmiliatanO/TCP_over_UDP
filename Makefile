CC:=g++
FLAG:=-std=c++23 -Wall -Wextra -O2 -lpthread
LFLAG:=-lpthread
SERV_OBJ:=server.o
SERV_ELF:=server.elf
CLIE_OBJ:=client.o
CLIE_ELF:=client.elf
SERV_SOURCE:=server.cpp
CLIE_SOURCE:=client.cpp
HEADER_DIR:=src/
HEADER:=src/tcp.h src/tcp_para.h src/tcp_struct.h src/tcp_utili.h

dep: $(SOURCE) $(HEADER_DIR) $(HEADER)
	$(CC) $(FLAG) -I $(HEADER_DIR) -c $(CLIE_SOURCE)
	$(CC) $(FLAG) -I $(HEADER_DIR) -c $(SERV_SOURCE)

	$(CC) $(CLIE_OBJ) -o $(CLIE_ELF) $(LFLAG)
	$(CC) $(SERV_OBJ) -o $(SERV_ELF) $(LFLAG)

all: dep

.PHONY: clean
clean:
	-rm -rf *.o

