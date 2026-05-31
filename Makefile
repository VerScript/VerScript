CC = gcc
CFLAGS = -Iinclude -Wall -Wextra
SRC = src/main.c
OBJ = $(SRC:.c=.o)
TARGET = verscript

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^

clean:
	rm -f src/*.o $(TARGET)
