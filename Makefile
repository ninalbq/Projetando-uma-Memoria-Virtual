CC = gcc
CFLAGS = -Wall -Wextra -Werror -pthread -O2
TARGET = vm
SRC = vm.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) correct.txt