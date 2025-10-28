CC = cc
CFLAGS = -O2 -Wall -Wextra -pedantic -std=c11
BIN_DIR = bin
BIN = $(BIN_DIR)/raw_server
SRC = src/server.c

all: $(BIN)

$(BIN):
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

clean:
	rm -rf $(BIN_DIR)
