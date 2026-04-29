#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 64
#define MAX_CMDS 16
#define MAX_LINE 2048

typedef struct {
    char *argv[MAX_ARGS];
    int argc;
    char *infile;
    char *outfile;
    int append;
} Command;

static int g_last_status;

static void trim_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) s[--len] = '\0';
}

static char *trim_spaces(char *s) {
    char *end;
    while (*s && isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) *(--end) = '\0';
    return s;
}

static int status_from_wait(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

static void reap_background_children(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[done] pid=%ld status=%d\n", (long)pid, status_from_wait(status));
    }
}

static int parse_line(char *line, char *argv[]) {
    int argc = 0;
    char *p = line;

    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (argc >= MAX_ARGS - 1) {
            fprintf(stderr, "too many arguments\n");
            return -1;
        }

        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            argv[argc++] = p;
            while (*p && *p != quote) p++;
            if (*p != quote) {
                fprintf(stderr, "unterminated quote\n");
                return -1;
            }
            *p++ = '\0';
            continue;
        }

        if (*p == '>' || *p == '<' || *p == '|' || *p == '&') {
            if (*p == '>' && *(p + 1) == '>') {
                argv[argc++] = ">>";
                p += 2;
            } else {
                argv[argc++] = (*p == '>') ? ">" : (*p == '<') ? "<" : (*p == '|') ? "|" : "&";
                p++;
            }
            continue;
        }

        argv[argc++] = p;
        while (*p && !isspace((unsigned char)*p) && *p != '>' && *p != '<' && *p != '|' && *p != '&') p++;
        if (*p) *p++ = '\0';
    }

    argv[argc] = NULL;
    return argc;
}

static void expand_environment_variables(int argc, char *argv[]) {
    for (int i = 0; i < argc; ++i) {
        if (argv[i][0] == '$' && argv[i][1] != '\0') {
            const char *value = getenv(argv[i] + 1);
            argv[i] = (char *)(value ? value : "");
        }
    }
}

static int parse_commands(int argc, char *tokens[], Command cmds[], int *cmd_count) {
    int current = 0;
    if (argc == 0) return -1;
    memset(cmds, 0, sizeof(Command) * MAX_CMDS);

    for (int i = 0; i < argc; ++i) {
        if (strcmp(tokens[i], "|") == 0) {
            if (cmds[current].argc == 0) {
                fprintf(stderr, "invalid pipe usage\n");
                return -1;
            }
            cmds[current].argv[cmds[current].argc] = NULL;
            if (++current >= MAX_CMDS) {
                fprintf(stderr, "too many piped commands\n");
                return -1;
            }
            continue;
        }

        if (strcmp(tokens[i], "<") == 0) {
            if (cmds[current].infile || i + 1 >= argc) {
                fprintf(stderr, "syntax error near '<'\n");
                return -1;
            }
            cmds[current].infile = tokens[++i];
            continue;
        }

        if (strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], ">>") == 0) {
            if (cmds[current].outfile || i + 1 >= argc) {
                fprintf(stderr, "syntax error near '>'\n");
                return -1;
            }
            cmds[current].outfile = tokens[++i];
            cmds[current].append = (strcmp(tokens[i - 1], ">>") == 0);
            continue;
        }

        if (strcmp(tokens[i], "&") == 0) {
            fprintf(stderr, "'&' must be at the end\n");
            return -1;
        }

        if (cmds[current].argc >= MAX_ARGS - 1) {
            fprintf(stderr, "too many arguments\n");
            return -1;
        }
        cmds[current].argv[cmds[current].argc++] = tokens[i];
    }

    if (cmds[current].argc == 0) {
        fprintf(stderr, "empty command\n");
        return -1;
    }
    cmds[current].argv[cmds[current].argc] = NULL;
    *cmd_count = current + 1;
    return 0;
}

static int apply_redirection(const char *infile, const char *outfile, int append) {
    if (infile) {
        int fd = open(infile, O_RDONLY);
        if (fd == -1) {
            perror("open input");
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("dup2 input");
            close(fd);
            return -1;
        }
        close(fd);
    }

    if (outfile) {
        int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        int fd = open(outfile, flags, 0644);
        if (fd == -1) {
            perror("open output");
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) == -1) {
            perror("dup2 output");
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

static int is_builtin(const char *cmd) {
    return strcmp(cmd, "exit") == 0 || strcmp(cmd, "cd") == 0 || strcmp(cmd, "pwd") == 0 ||
           strcmp(cmd, "status") == 0 || strcmp(cmd, "help") == 0;
}

static int run_builtin(int argc, char *argv[], int *should_exit) {
    *should_exit = 0;
    if (argc == 0) return 0;

    if (strcmp(argv[0], "exit") == 0) {
        *should_exit = 1;
        return 0;
    }

    if (strcmp(argv[0], "cd") == 0) {
        const char *path = (argc > 1) ? argv[1] : getenv("HOME");
        if (!path) {
            fprintf(stderr, "cd: HOME not set\n");
            return 1;
        }
        if (chdir(path) == -1) {
            perror("cd");
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[0], "pwd") == 0) {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("pwd");
            return 1;
        }
        puts(cwd);
        return 0;
    }

    if (strcmp(argv[0], "status") == 0) {
        printf("%d\n", g_last_status);
        return 0;
    }

    if (strcmp(argv[0], "help") == 0) {
        puts("builtins: cd pwd status help exit");
        puts("features: quotes, < > >>, pipes, &, $VAR");
        return 0;
    }

    return -1;
}

static int run_builtin_with_redirection(Command *cmd, int *should_exit) {
    int saved_stdin = -1, saved_stdout = -1;

    if (cmd->infile || cmd->outfile) {
        saved_stdin = dup(STDIN_FILENO);
        saved_stdout = dup(STDOUT_FILENO);
        if (saved_stdin == -1 || saved_stdout == -1) {
            perror("dup");
            if (saved_stdin != -1) close(saved_stdin);
            if (saved_stdout != -1) close(saved_stdout);
            return 1;
        }
        if (apply_redirection(cmd->infile, cmd->outfile, cmd->append) != 0) {
            close(saved_stdin);
            close(saved_stdout);
            return 1;
        }
    }

    int result = run_builtin(cmd->argc, cmd->argv, should_exit);

    if (saved_stdin != -1) {
        if (dup2(saved_stdin, STDIN_FILENO) == -1) perror("restore stdin");
        close(saved_stdin);
    }
    if (saved_stdout != -1) {
        if (dup2(saved_stdout, STDOUT_FILENO) == -1) perror("restore stdout");
        close(saved_stdout);
    }
    return result;
}

static int run_external(Command *cmd, int background) {
    int status;
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        if (apply_redirection(cmd->infile, cmd->outfile, cmd->append) != 0) _exit(1);
        execvp(cmd->argv[0], cmd->argv);
        perror("execvp");
        _exit(127);
    }

    if (background) {
        printf("[bg] pid=%ld\n", (long)pid);
        return 0;
    }

    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return 1;
    }
    return status_from_wait(status);
}

static int run_pipeline(Command cmds[], int cmd_count, int background) {
    int pipes[MAX_CMDS - 1][2];
    pid_t pids[MAX_CMDS];
    int last_status = 0;

    for (int i = 0; i < cmd_count - 1; ++i) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            for (int j = 0; j < i; ++j) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return 1;
        }
    }

    for (int i = 0; i < cmd_count; ++i) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            for (int k = 0; k < cmd_count - 1; ++k) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            for (int k = 0; k < i; ++k) waitpid(pids[k], NULL, 0);
            return 1;
        }

        if (pids[i] == 0) {
            signal(SIGINT, SIG_DFL);
            if (i > 0 && dup2(pipes[i - 1][0], STDIN_FILENO) == -1) {
                perror("dup2 pipe input");
                _exit(1);
            }
            if (i < cmd_count - 1 && dup2(pipes[i][1], STDOUT_FILENO) == -1) {
                perror("dup2 pipe output");
                _exit(1);
            }
            for (int k = 0; k < cmd_count - 1; ++k) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            if (apply_redirection(cmds[i].infile, cmds[i].outfile, cmds[i].append) != 0) _exit(1);
            execvp(cmds[i].argv[0], cmds[i].argv);
            perror("execvp");
            _exit(127);
        }
    }

    for (int i = 0; i < cmd_count - 1; ++i) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if (background) {
        printf("[bg] pipeline started\n");
        return 0;
    }

    for (int i = 0; i < cmd_count; ++i) {
        int status;
        if (waitpid(pids[i], &status, 0) == -1) {
            perror("waitpid");
            last_status = 1;
            continue;
        }
        if (i == cmd_count - 1) last_status = status_from_wait(status);
    }
    return last_status;
}

static int execute_line(char *line, int *should_exit) {
    char *argv[MAX_ARGS];
    Command cmds[MAX_CMDS];
    int argc, cmd_count, background = 0;

    *should_exit = 0;
    argc = parse_line(line, argv);
    if (argc <= 0) return (argc < 0) ? 1 : 0;

    expand_environment_variables(argc, argv);

    if (strcmp(argv[argc - 1], "&") == 0) {
        background = 1;
        argv[--argc] = NULL;
    }
    if (argc == 0) return 0;

    if (parse_commands(argc, argv, cmds, &cmd_count) != 0) return 1;

    if (cmd_count == 1 && is_builtin(cmds[0].argv[0])) {
        if (background) {
            fprintf(stderr, "background mode is not supported for builtin commands\n");
            return 1;
        }
        int code = run_builtin_with_redirection(&cmds[0], should_exit);
        return (code < 0) ? 1 : code;
    }

    if (cmd_count == 1) return run_external(&cmds[0], background);
    return run_pipeline(cmds, cmd_count, background);
}

int main(void) {
    char *line = NULL;
    size_t cap = 0;

    signal(SIGINT, SIG_IGN);

    while (1) {
        int should_exit = 0;
        char *command_text;

        reap_background_children();

        printf("mini-shell$ ");
        fflush(stdout);

        if (getline(&line, &cap, stdin) == -1) {
            if (feof(stdin)) {
                putchar('\n');
                break;
            }
            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }
            perror("getline");
            break;
        }

        trim_newline(line);
        command_text = trim_spaces(line);
        if (command_text[0] == '\0') continue;

        g_last_status = execute_line(command_text, &should_exit);
        if (should_exit) break;
    }

    free(line);
    return 0;
}
