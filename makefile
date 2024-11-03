CC = gcc
CFLAGS = -Wall 

all: clean compile run

compile: server client

server: server.c
	$(CC) $(CFLAGS) -o server server.c 

client: client.c
	$(CC) $(CFLAGS) -o client client.c

run:
	@echo "Starting server..."
	@./server "/home/alper/Masaüstü/serverfile" 10 & echo $$! > server.pid
	@echo "Server started with PID `cat server.pid`"
	@./client Connect `cat server.pid`

clean:
	rm -f server client server.pid
