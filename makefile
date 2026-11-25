CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -g
TARGET  = nabla
SRC     = src/main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

dot: run
	dot -Tpng program.dot -o program.png

clean:
	rm -f $(TARGET) *.o program.dot program.png

.PHONY: all run dot clean
