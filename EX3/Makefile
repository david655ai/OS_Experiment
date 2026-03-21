CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
LDFLAGS =

TARGETS = mini_shell pipe_ipc msgqueue_threads shm_processes

all: $(TARGETS)

mini_shell: mini_shell.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

pipe_ipc: pipe_ipc.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

msgqueue_threads: msgqueue_threads.c
	$(CC) $(CFLAGS) -o $@ $< -pthread -lrt $(LDFLAGS)

shm_processes: shm_processes.c
	$(CC) $(CFLAGS) -o $@ $< -pthread -lrt $(LDFLAGS)

clean:
	rm -f $(TARGETS)

.PHONY: all clean
