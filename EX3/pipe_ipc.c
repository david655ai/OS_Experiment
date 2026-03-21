#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    int p2c[2];
    int c2p[2];
    pid_t pid;
    const char *requests[] = {
        "get time",
        "get status",
        "quit soon"
    };
    size_t request_count = sizeof(requests) / sizeof(requests[0]);

    if (pipe(p2c) == -1) {
        perror("pipe p2c");
        return 1;
    }

    if (pipe(c2p) == -1) {
        perror("pipe c2p");
        close(p2c[0]);
        close(p2c[1]);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        close(p2c[0]);
        close(p2c[1]);
        close(c2p[0]);
        close(c2p[1]);
        return 1;
    }

    if (pid == 0) {
        FILE *reader;
        FILE *writer;
        char req[256];
        int seq = 1;

        close(p2c[1]);
        close(c2p[0]);

        reader = fdopen(p2c[0], "r");
        writer = fdopen(c2p[1], "w");
        if (reader == NULL || writer == NULL) {
            perror("fdopen");
            if (reader != NULL) {
                fclose(reader);
            } else {
                close(p2c[0]);
            }
            if (writer != NULL) {
                fclose(writer);
            } else {
                close(c2p[1]);
            }
            _exit(1);
        }

        while (fgets(req, sizeof(req), reader) != NULL) {
            size_t len = strlen(req);
            if (len > 0 && req[len - 1] == '\n') {
                req[len - 1] = '\0';
            }

            fprintf(writer, "response %d: child processed '%s'\n", seq++, req);
            fflush(writer);
        }

        fclose(reader);
        fclose(writer);
        _exit(0);
    }

    {
        FILE *writer;
        FILE *reader;
        char resp[256];

        close(p2c[0]);
        close(c2p[1]);

        writer = fdopen(p2c[1], "w");
        reader = fdopen(c2p[0], "r");
        if (writer == NULL || reader == NULL) {
            perror("fdopen");
            if (writer != NULL) {
                fclose(writer);
            } else {
                close(p2c[1]);
            }
            if (reader != NULL) {
                fclose(reader);
            } else {
                close(c2p[0]);
            }
            waitpid(pid, NULL, 0);
            return 1;
        }

        for (size_t i = 0; i < request_count; ++i) {
            fprintf(writer, "%s\n", requests[i]);
            fflush(writer);
            printf("parent sent: %s\n", requests[i]);
        }

        /* Closing write end sends EOF to child reader. */
        fclose(writer);

        while (fgets(resp, sizeof(resp), reader) != NULL) {
            printf("parent received: %s", resp);
        }

        fclose(reader);
    }

    waitpid(pid, NULL, 0);
    return 0;
}
