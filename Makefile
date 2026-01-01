CC = gcc
CFLAGS = -Wall -I./lib/paho.mqtt.c-1.3.13/src
LDFLAGS = -lpaho-mqtt3c -lmicrohttpd -lpthread -ljson-c -lsqlite3

all:
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/db.c -o build/db.o
	$(CC) $(CFLAGS) -c src/shared.c -o build/shared.o
	$(CC) $(CFLAGS) -c src/mqtt.c -o build/mqtt.o
	$(CC) $(CFLAGS) -c src/http_api.c -o build/http_api.o
	$(CC) $(CFLAGS) -c src/main.c -o build/main.o
	$(CC) -o build/server build/main.o build/db.o build/shared.o build/mqtt.o build/http_api.o $(LDFLAGS)

clean:
	rm -rf build/*

run:
	./build/server

.PHONY: all clean run