CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra
LDFLAGS =

TARGET = myfs
SRCS = main.c fs.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean run rebuild

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c fs.h
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET) vdisk.img

rebuild: clean all
