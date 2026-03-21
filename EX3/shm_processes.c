#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PAYLOAD_SIZE 128
#define REQUEST_COUNT 5

typedef struct {
    int req_id;
    uint64_t req_ts_ns;
    int shutdown;
    char req_payload[PAYLOAD_SIZE];
    char resp_payload[PAYLOAD_SIZE];
} SharedBlock;

typedef struct {
    int requests_sent;
    int responses_received;
    int request_timeouts;
    uint64_t total_rtt_ns;
} ParentStats;

static char g_shm_name[64];
static char g_req_sem_name[64];
static char g_resp_sem_name[64];

static int g_shm_fd = -1;
static SharedBlock *g_shared = NULL;
static sem_t *g_req_sem = SEM_FAILED;
static sem_t *g_resp_sem = SEM_FAILED;
static int g_is_parent = 1;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int timed_sem_wait(sem_t *sem, int timeout_sec) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_sec;

    while (sem_timedwait(sem, &deadline) == -1) {
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
    return 0;
}

static void cleanup_resources(void) {
    if (g_req_sem != SEM_FAILED) {
        sem_close(g_req_sem);
        g_req_sem = SEM_FAILED;
    }

    if (g_resp_sem != SEM_FAILED) {
        sem_close(g_resp_sem);
        g_resp_sem = SEM_FAILED;
    }

    if (g_shared != NULL) {
        munmap(g_shared, sizeof(*g_shared));
        g_shared = NULL;
    }

    if (g_shm_fd != -1) {
        close(g_shm_fd);
        g_shm_fd = -1;
    }

    if (g_is_parent) {
        if (g_req_sem_name[0] != '\0') {
            sem_unlink(g_req_sem_name);
        }
        if (g_resp_sem_name[0] != '\0') {
            sem_unlink(g_resp_sem_name);
        }
        if (g_shm_name[0] != '\0') {
            shm_unlink(g_shm_name);
        }
    }
}

static int child_loop(void) {
    int processed = 0;

    while (1) {
        if (timed_sem_wait(g_req_sem, 3) == -1) {
            if (errno == ETIMEDOUT) {
                printf("child timeout: no request within 3s\n");
                continue;
            }
            perror("child sem_timedwait req");
            return 1;
        }

        if (g_shared->shutdown) {
            break;
        }

        processed++;
        printf("child <- request id=%d payload=%s\n", g_shared->req_id, g_shared->req_payload);
        snprintf(g_shared->resp_payload,
                 sizeof(g_shared->resp_payload),
                 "ack_%.100s_by_child",
                 g_shared->req_payload);

        if (sem_post(g_resp_sem) == -1) {
            perror("child sem_post resp");
            return 1;
        }
    }

    printf("child processed total: %d\n", processed);
    return 0;
}

int main(void) {
    ParentStats stats = {0};
    pid_t pid;

    snprintf(g_shm_name, sizeof(g_shm_name), "/ex3_shm_%ld", (long)getpid());
    snprintf(g_req_sem_name, sizeof(g_req_sem_name), "/ex3_req_sem_%ld", (long)getpid());
    snprintf(g_resp_sem_name, sizeof(g_resp_sem_name), "/ex3_resp_sem_%ld", (long)getpid());

    g_shm_fd = shm_open(g_shm_name, O_CREAT | O_RDWR, 0600);
    if (g_shm_fd == -1) {
        perror("shm_open");
        return 1;
    }

    if (ftruncate(g_shm_fd, (off_t)sizeof(SharedBlock)) == -1) {
        perror("ftruncate");
        cleanup_resources();
        return 1;
    }

    g_shared = mmap(NULL, sizeof(*g_shared), PROT_READ | PROT_WRITE, MAP_SHARED, g_shm_fd, 0);
    if (g_shared == MAP_FAILED) {
        g_shared = NULL;
        perror("mmap");
        cleanup_resources();
        return 1;
    }

    g_req_sem = sem_open(g_req_sem_name, O_CREAT | O_EXCL, 0600, 0);
    if (g_req_sem == SEM_FAILED) {
        perror("sem_open request");
        cleanup_resources();
        return 1;
    }

    g_resp_sem = sem_open(g_resp_sem_name, O_CREAT | O_EXCL, 0600, 0);
    if (g_resp_sem == SEM_FAILED) {
        perror("sem_open response");
        cleanup_resources();
        return 1;
    }

    memset(g_shared, 0, sizeof(*g_shared));

    pid = fork();
    if (pid < 0) {
        perror("fork");
        cleanup_resources();
        return 1;
    }

    if (pid == 0) {
        int child_code;

        g_is_parent = 0;
        child_code = child_loop();
        cleanup_resources();
        _exit(child_code == 0 ? 0 : 1);
    }

    for (int i = 1; i <= REQUEST_COUNT; ++i) {
        uint64_t ts = now_ns();

        g_shared->req_id = i;
        g_shared->req_ts_ns = ts;
        g_shared->shutdown = 0;
        snprintf(g_shared->req_payload, sizeof(g_shared->req_payload), "task_%d", i);

        if (sem_post(g_req_sem) == -1) {
            perror("parent sem_post req");
            break;
        }

        stats.requests_sent++;
        printf("parent -> request id=%d payload=%s\n", g_shared->req_id, g_shared->req_payload);

        if (timed_sem_wait(g_resp_sem, 2) == -1) {
            if (errno == ETIMEDOUT) {
                stats.request_timeouts++;
                printf("parent timeout: waiting response for request %d\n", i);
                continue;
            }
            perror("parent sem_timedwait resp");
            break;
        }

        {
            uint64_t rtt = now_ns() - g_shared->req_ts_ns;
            stats.responses_received++;
            stats.total_rtt_ns += rtt;
            printf("parent <- response id=%d payload=%s rtt=%.2f ms\n",
                   g_shared->req_id,
                   g_shared->resp_payload,
                   (double)rtt / 1000000.0);
        }
    }

    g_shared->shutdown = 1;
    if (sem_post(g_req_sem) == -1) {
        perror("parent sem_post shutdown");
    }

    if (waitpid(pid, NULL, 0) == -1) {
        perror("waitpid");
    }

    {
        double avg_ms = (stats.responses_received > 0)
                            ? ((double)stats.total_rtt_ns / (double)stats.responses_received) / 1000000.0
                            : 0.0;
        puts("\n=== statistics ===");
        printf("requests sent:      %d\n", stats.requests_sent);
        printf("responses received: %d\n", stats.responses_received);
        printf("response timeouts:  %d\n", stats.request_timeouts);
        printf("average RTT:        %.2f ms\n", avg_ms);
    }

    cleanup_resources();

    return 0;
}
