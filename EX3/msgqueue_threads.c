#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MSG_COUNT 5
#define MSG_SIZE 256

typedef struct {
    int req_sent;
    int req_received;
    int resp_sent;
    int resp_received;
    int req_timeouts;
    uint64_t total_latency_ns;
} Stats;

static mqd_t g_req_mq;
static mqd_t g_resp_mq;
static Stats g_stats;
static pthread_mutex_t g_stats_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void add_stat(int *field, int delta) {
    pthread_mutex_lock(&g_stats_lock);
    *field += delta;
    pthread_mutex_unlock(&g_stats_lock);
}

static void add_latency(uint64_t latency_ns) {
    pthread_mutex_lock(&g_stats_lock);
    g_stats.total_latency_ns += latency_ns;
    pthread_mutex_unlock(&g_stats_lock);
}

static int parse_message(const char *msg, int *id, uint64_t *send_ts, char *payload, size_t payload_size) {
    const char *first = strchr(msg, '|');
    const char *second;
    long long id_val;
    unsigned long long ts_val;

    if (first == NULL) {
        return -1;
    }

    second = strchr(first + 1, '|');
    if (second == NULL) {
        return -1;
    }

    id_val = strtoll(msg, NULL, 10);
    ts_val = strtoull(first + 1, NULL, 10);

    *id = (int)id_val;
    *send_ts = (uint64_t)ts_val;

    if (payload_size == 0) {
        return -1;
    }

    snprintf(payload, payload_size, "%s", second + 1);
    return 0;
}

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void *sender_thread(void *arg) {
    (void)arg;
    char req[MSG_SIZE];
    char resp[MSG_SIZE];
    unsigned int prio;

    sleep(2);

    for (int i = 1; i <= MSG_COUNT; ++i) {
        uint64_t ts = now_ns();
        unsigned int send_prio = (i % 3 == 0) ? 2U : ((i % 2 == 0) ? 1U : 0U);

        snprintf(req, sizeof(req), "%d|%llu|task_%d", i, (unsigned long long)ts, i);
        if (mq_send(g_req_mq, req, strlen(req) + 1, send_prio) == -1) {
            perror("mq_send request");
            return NULL;
        }

        add_stat(&g_stats.req_sent, 1);
        printf("sender -> request id=%d prio=%u payload=task_%d\n", i, send_prio, i);
        sleep_ms(200);
    }

    if (mq_send(g_req_mq, "-1|0|exit", 10, 3) == -1) {
        perror("mq_send exit");
        return NULL;
    }

    for (int i = 1; i <= MSG_COUNT; ++i) {
        int id;
        uint64_t send_ts;
        char payload[MSG_SIZE];
        uint64_t rtt;

        ssize_t n = mq_receive(g_resp_mq, resp, sizeof(resp), &prio);
        if (n == -1) {
            perror("mq_receive response");
            return NULL;
        }

        if (parse_message(resp, &id, &send_ts, payload, sizeof(payload)) != 0) {
            fprintf(stderr, "sender parse response failed: %s\n", resp);
            return NULL;
        }

        rtt = now_ns() - send_ts;
        add_stat(&g_stats.resp_received, 1);
        add_latency(rtt);
        printf("sender <- response id=%d prio=%u payload=%s rtt=%.2f ms\n",
               id,
               prio,
               payload,
               (double)rtt / 1000000.0);
    }

    return NULL;
}

static void *receiver_thread(void *arg) {
    (void)arg;
    char req[MSG_SIZE];
    char response[MSG_SIZE];

    while (1) {
        struct timespec deadline;
        unsigned int req_prio;
        int id;
        uint64_t send_ts;
        char payload[MSG_SIZE];

        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += 1;

        ssize_t n = mq_timedreceive(g_req_mq, req, sizeof(req), &req_prio, &deadline);
        if (n == -1) {
            if (errno == ETIMEDOUT) {
                add_stat(&g_stats.req_timeouts, 1);
                printf("receiver timeout: no request within 1s\n");
                continue;
            }
            if (errno == EINTR) {
                continue;
            }
            perror("mq_timedreceive request");
            return NULL;
        }

        if (parse_message(req, &id, &send_ts, payload, sizeof(payload)) != 0) {
            fprintf(stderr, "receiver parse request failed: %s\n", req);
            return NULL;
        }

        if (id == -1 && strcmp(payload, "exit") == 0) {
            printf("receiver got shutdown signal\n");
            break;
        }

        add_stat(&g_stats.req_received, 1);
        printf("receiver <- request id=%d prio=%u payload=%s\n", id, req_prio, payload);

        snprintf(response, sizeof(response), "%d|%llu|ack_%.180s", id, (unsigned long long)send_ts, payload);
        if (mq_send(g_resp_mq, response, strlen(response) + 1, req_prio) == -1) {
            perror("mq_send response");
            return NULL;
        }
        add_stat(&g_stats.resp_sent, 1);
        printf("receiver -> response id=%d prio=%u payload=ack_%s\n", id, req_prio, payload);
    }

    return NULL;
}

int main(void) {
    pthread_t sender;
    pthread_t receiver;
    struct mq_attr attr;
    char req_name[64];
    char resp_name[64];

    snprintf(req_name, sizeof(req_name), "/ex3_req_%ld", (long)getpid());
    snprintf(resp_name, sizeof(resp_name), "/ex3_resp_%ld", (long)getpid());

    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MSG_SIZE;

    g_req_mq = mq_open(req_name, O_CREAT | O_RDWR, 0600, &attr);
    if (g_req_mq == (mqd_t)-1) {
        perror("mq_open request");
        return 1;
    }

    g_resp_mq = mq_open(resp_name, O_CREAT | O_RDWR, 0600, &attr);
    if (g_resp_mq == (mqd_t)-1) {
        perror("mq_open response");
        mq_close(g_req_mq);
        mq_unlink(req_name);
        return 1;
    }

    if (pthread_create(&sender, NULL, sender_thread, NULL) != 0) {
        perror("pthread_create sender");
        mq_close(g_req_mq);
        mq_close(g_resp_mq);
        mq_unlink(req_name);
        mq_unlink(resp_name);
        return 1;
    }

    if (pthread_create(&receiver, NULL, receiver_thread, NULL) != 0) {
        perror("pthread_create receiver");
        pthread_join(sender, NULL);
        mq_close(g_req_mq);
        mq_close(g_resp_mq);
        mq_unlink(req_name);
        mq_unlink(resp_name);
        return 1;
    }

    pthread_join(sender, NULL);
    pthread_join(receiver, NULL);

    if (mq_close(g_req_mq) == -1) {
        perror("mq_close request");
    }
    if (mq_close(g_resp_mq) == -1) {
        perror("mq_close response");
    }
    if (mq_unlink(req_name) == -1) {
        perror("mq_unlink request");
    }
    if (mq_unlink(resp_name) == -1) {
        perror("mq_unlink response");
    }

    {
        double avg_ms = (g_stats.resp_received > 0)
                            ? ((double)g_stats.total_latency_ns / (double)g_stats.resp_received) / 1000000.0
                            : 0.0;
        puts("\n=== statistics ===");
        printf("requests sent:      %d\n", g_stats.req_sent);
        printf("requests received:  %d\n", g_stats.req_received);
        printf("responses sent:     %d\n", g_stats.resp_sent);
        printf("responses received: %d\n", g_stats.resp_received);
        printf("receive timeouts:   %d\n", g_stats.req_timeouts);
        printf("average RTT:        %.2f ms\n", avg_ms);
    }

    return 0;
}
