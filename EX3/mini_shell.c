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
#define HISTORY_SIZE 50
#define MAX_ALIASES 32
#define MAX_JOBS 32
#define MAX_BG_CHILDREN 256
#define MAX_SEGMENTS 32
#define MAX_LINE 2048

enum {
    OP_SEQ = 0,
    OP_AND = 1,
    OP_OR = 2
};

typedef struct {
    char *argv[MAX_ARGS];
    int argc;
    char *infile;
    char *outfile;
    int append;
} Command;

typedef struct {
    char *name;
    char *value;
} Alias;

typedef struct {
    int id;
    pid_t pgid;
    int remaining;
    char *command;
} Job;

typedef struct {
    pid_t pid;
    int job_id;
} ChildRef;

static char *g_history[HISTORY_SIZE];
static int g_history_count;
static int g_last_status;

static Alias g_aliases[MAX_ALIASES];
static int g_alias_count;

static Job g_jobs[MAX_JOBS];
static int g_job_count;
static int g_next_job_id = 1;

static ChildRef g_child_refs[MAX_BG_CHILDREN];
static int g_child_ref_count;
static volatile sig_atomic_t g_foreground_pgid;

static int execute_line(char *line, int *should_exit);

static void trim_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

static char *trim_spaces(char *s) {
    char *end;

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }

    end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) {
        *(--end) = '\0';
    }

    return s;
}

static int status_from_wait(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

static void add_history(const char *line) {
    char *copy;

    if (line == NULL || line[0] == '\0') {
        return;
    }

    copy = strdup(line);
    if (copy == NULL) {
        perror("strdup");
        return;
    }

    if (g_history_count < HISTORY_SIZE) {
        g_history[g_history_count++] = copy;
        return;
    }

    free(g_history[0]);
    memmove(&g_history[0], &g_history[1], sizeof(g_history[0]) * (HISTORY_SIZE - 1));
    g_history[HISTORY_SIZE - 1] = copy;
}

static void print_history(void) {
    for (int i = 0; i < g_history_count; ++i) {
        printf("%d  %s\n", i + 1, g_history[i]);
    }
}

static int resolve_history_reference(const char *token, const char **resolved) {
    if (strcmp(token, "!!") == 0) {
        if (g_history_count == 0) {
            fprintf(stderr, "history is empty\n");
            return -1;
        }
        *resolved = g_history[g_history_count - 1];
        return 0;
    }

    if (isdigit((unsigned char)token[1])) {
        char *end;
        long idx = strtol(token + 1, &end, 10);

        if (*end != '\0' || idx <= 0 || idx > g_history_count) {
            fprintf(stderr, "history event not found: %s\n", token);
            return -1;
        }

        *resolved = g_history[idx - 1];
        return 0;
    }

    {
        const char *prefix = token + 1;
        size_t len = strlen(prefix);

        for (int i = g_history_count - 1; i >= 0; --i) {
            if (strncmp(g_history[i], prefix, len) == 0) {
                *resolved = g_history[i];
                return 0;
            }
        }
    }

    fprintf(stderr, "history event not found: %s\n", token);
    return -1;
}

static void sigint_handler(int signo) {
    (void)signo;
    if (g_foreground_pgid > 0) {
        kill(-g_foreground_pgid, SIGINT);
    }
}

static int parse_line(char *line, char *argv[]) {
    int argc = 0;
    char *p = line;

    while (*p != '\0') {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        if (argc >= MAX_ARGS - 1) {
            fprintf(stderr, "too many arguments\n");
            return -1;
        }

        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            argv[argc++] = p;
            while (*p != '\0' && *p != quote) {
                p++;
            }
            if (*p != quote) {
                fprintf(stderr, "unterminated quote\n");
                return -1;
            }
            *p = '\0';
            p++;
            continue;
        }

        if (*p == '>' || *p == '<' || *p == '|' || *p == '&') {
            if (*p == '>' && *(p + 1) == '>') {
                argv[argc++] = ">>";
                p += 2;
            } else {
                if (*p == '>') {
                    argv[argc++] = ">";
                } else if (*p == '<') {
                    argv[argc++] = "<";
                } else if (*p == '|') {
                    argv[argc++] = "|";
                } else {
                    argv[argc++] = "&";
                }
                p++;
            }
            continue;
        }

        argv[argc++] = p;
        while (*p != '\0' && !isspace((unsigned char)*p) && *p != '>' && *p != '<' && *p != '|' && *p != '&') {
            p++;
        }
        if (*p != '\0') {
            *p = '\0';
            p++;
        }
    }

    argv[argc] = NULL;
    return argc;
}

static int split_chained_commands(char *line, char *segments[], int ops[]) {
    int count = 0;
    char *start = line;
    char quote = '\0';
    char *p = line;

    ops[0] = OP_SEQ;

    while (*p != '\0') {
        if (quote != '\0') {
            if (*p == quote) {
                quote = '\0';
            }
            p++;
            continue;
        }

        if (*p == '"' || *p == '\'') {
            quote = *p;
            p++;
            continue;
        }

        if (*p == ';' || (*p == '&' && *(p + 1) == '&') || (*p == '|' && *(p + 1) == '|')) {
            int op_for_next = OP_SEQ;

            if (*p == ';') {
                *p = '\0';
                p++;
            } else if (*p == '&') {
                *p = '\0';
                *(p + 1) = '\0';
                p += 2;
                op_for_next = OP_AND;
            } else {
                *p = '\0';
                *(p + 1) = '\0';
                p += 2;
                op_for_next = OP_OR;
            }

            if (count >= MAX_SEGMENTS - 1) {
                fprintf(stderr, "too many chained commands\n");
                return -1;
            }

            segments[count] = trim_spaces(start);
            count++;
            ops[count] = op_for_next;
            start = p;
            continue;
        }

        p++;
    }

    if (count >= MAX_SEGMENTS) {
        fprintf(stderr, "too many chained commands\n");
        return -1;
    }

    segments[count++] = trim_spaces(start);
    return count;
}

static int parse_commands(int argc, char *tokens[], Command cmds[], int *cmd_count) {
    int current = 0;

    if (argc == 0) {
        return -1;
    }

    memset(cmds, 0, sizeof(Command) * MAX_CMDS);

    for (int i = 0; i < argc; ++i) {
        if (strcmp(tokens[i], "|") == 0) {
            if (cmds[current].argc == 0) {
                fprintf(stderr, "invalid pipe usage\n");
                return -1;
            }
            cmds[current].argv[cmds[current].argc] = NULL;
            current++;
            if (current >= MAX_CMDS) {
                fprintf(stderr, "too many piped commands\n");
                return -1;
            }
            continue;
        }

        if (strcmp(tokens[i], "<") == 0) {
            if (cmds[current].infile != NULL || i + 1 >= argc) {
                fprintf(stderr, "syntax error near '<'\n");
                return -1;
            }
            cmds[current].infile = tokens[++i];
            continue;
        }

        if (strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], ">>") == 0) {
            if (cmds[current].outfile != NULL || i + 1 >= argc) {
                fprintf(stderr, "syntax error near '>'\n");
                return -1;
            }
            cmds[current].outfile = tokens[++i];
            cmds[current].append = (strcmp(tokens[i - 1], ">>") == 0);
            continue;
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
    if (infile != NULL) {
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

    if (outfile != NULL) {
        int flags = O_WRONLY | O_CREAT;
        int fd;

        if (append) {
            flags |= O_APPEND;
        } else {
            flags |= O_TRUNC;
        }

        fd = open(outfile, flags, 0644);
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

static void remove_child_ref_index(int idx) {
    if (idx < 0 || idx >= g_child_ref_count) {
        return;
    }
    g_child_refs[idx] = g_child_refs[g_child_ref_count - 1];
    g_child_ref_count--;
}

static int find_job_index_by_id(int id) {
    for (int i = 0; i < g_job_count; ++i) {
        if (g_jobs[i].id == id) {
            return i;
        }
    }
    return -1;
}

static void remove_job_index(int idx) {
    if (idx < 0 || idx >= g_job_count) {
        return;
    }
    free(g_jobs[idx].command);
    g_jobs[idx] = g_jobs[g_job_count - 1];
    g_job_count--;
}

static int add_job(pid_t pgid, pid_t pids[], int pid_count, const char *command) {
    char *cmd_copy;

    if (g_job_count >= MAX_JOBS) {
        fprintf(stderr, "job table is full\n");
        return -1;
    }

    if (g_child_ref_count + pid_count > MAX_BG_CHILDREN) {
        fprintf(stderr, "background child table is full\n");
        return -1;
    }

    cmd_copy = strdup(command);
    if (cmd_copy == NULL) {
        perror("strdup");
        return -1;
    }

    g_jobs[g_job_count].id = g_next_job_id++;
    g_jobs[g_job_count].pgid = pgid;
    g_jobs[g_job_count].remaining = pid_count;
    g_jobs[g_job_count].command = cmd_copy;

    for (int i = 0; i < pid_count; ++i) {
        g_child_refs[g_child_ref_count].pid = pids[i];
        g_child_refs[g_child_ref_count].job_id = g_jobs[g_job_count].id;
        g_child_ref_count++;
    }

    g_job_count++;
    return g_jobs[g_job_count - 1].id;
}

static void reap_background_jobs(void) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int ref_idx = -1;
        int job_id = -1;

        for (int i = 0; i < g_child_ref_count; ++i) {
            if (g_child_refs[i].pid == pid) {
                ref_idx = i;
                job_id = g_child_refs[i].job_id;
                break;
            }
        }

        if (ref_idx == -1) {
            continue;
        }

        remove_child_ref_index(ref_idx);

        for (int i = 0; i < g_job_count; ++i) {
            if (g_jobs[i].id == job_id) {
                g_jobs[i].remaining--;
                if (g_jobs[i].remaining <= 0) {
                    int code = status_from_wait(status);
                    printf("[job %d done] status=%d  %s\n", g_jobs[i].id, code, g_jobs[i].command);
                    remove_job_index(i);
                }
                break;
            }
        }
    }
}

static void print_jobs(void) {
    for (int i = 0; i < g_job_count; ++i) {
        printf("[%d] running  pgid=%ld  %s\n", g_jobs[i].id, (long)g_jobs[i].pgid, g_jobs[i].command);
    }
}

static int parse_job_id(const char *s) {
    char *end;
    long id;

    if (s == NULL) {
        return -1;
    }
    if (*s == '%') {
        s++;
    }

    if (*s == '\0') {
        return -1;
    }

    id = strtol(s, &end, 10);
    if (*end != '\0' || id <= 0) {
        return -1;
    }

    return (int)id;
}

static int builtin_fg(const char *arg) {
    int id = parse_job_id(arg);
    int job_idx;
    int final_status = 0;

    if (id < 0) {
        fprintf(stderr, "fg: usage fg <job-id>\n");
        return 1;
    }

    job_idx = find_job_index_by_id(id);
    if (job_idx < 0) {
        fprintf(stderr, "fg: job not found: %d\n", id);
        return 1;
    }

    if (kill(-g_jobs[job_idx].pgid, SIGCONT) == -1 && errno != ESRCH) {
        perror("fg");
        return 1;
    }

    printf("fg: %s\n", g_jobs[job_idx].command);
    g_foreground_pgid = g_jobs[job_idx].pgid;

    while (1) {
        int found_idx = -1;
        pid_t pid;
        int status;

        for (int i = 0; i < g_child_ref_count; ++i) {
            if (g_child_refs[i].job_id == id) {
                found_idx = i;
                break;
            }
        }

        if (found_idx < 0) {
            break;
        }

        pid = g_child_refs[found_idx].pid;
        if (waitpid(pid, &status, 0) == -1) {
            if (errno == ECHILD) {
                remove_child_ref_index(found_idx);
                continue;
            }
            perror("waitpid");
            g_foreground_pgid = 0;
            return 1;
        }

        final_status = status_from_wait(status);
        remove_child_ref_index(found_idx);

        job_idx = find_job_index_by_id(id);
        if (job_idx >= 0) {
            g_jobs[job_idx].remaining--;
            if (g_jobs[job_idx].remaining <= 0) {
                remove_job_index(job_idx);
            }
        }
    }

    job_idx = find_job_index_by_id(id);
    if (job_idx >= 0) {
        remove_job_index(job_idx);
    }

    g_foreground_pgid = 0;

    return final_status;
}

static int builtin_bg(const char *arg) {
    int id = parse_job_id(arg);
    int job_idx;

    if (id < 0) {
        fprintf(stderr, "bg: usage bg <job-id>\n");
        return 1;
    }

    job_idx = find_job_index_by_id(id);
    if (job_idx < 0) {
        fprintf(stderr, "bg: job not found: %d\n", id);
        return 1;
    }

    if (kill(-g_jobs[job_idx].pgid, SIGCONT) == -1) {
        perror("bg");
        return 1;
    }

    printf("[job %d resumed] %s\n", id, g_jobs[job_idx].command);
    return 0;
}

static int find_alias_index(const char *name) {
    for (int i = 0; i < g_alias_count; ++i) {
        if (strcmp(g_aliases[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void print_aliases(void) {
    for (int i = 0; i < g_alias_count; ++i) {
        printf("alias %s='%s'\n", g_aliases[i].name, g_aliases[i].value);
    }
}

static int set_alias(const char *name, const char *value) {
    int idx;
    char *name_copy;
    char *value_copy;

    if (name == NULL || value == NULL || name[0] == '\0') {
        fprintf(stderr, "alias: invalid name/value\n");
        return 1;
    }

    idx = find_alias_index(name);
    if (idx >= 0) {
        value_copy = strdup(value);
        if (value_copy == NULL) {
            perror("strdup");
            return 1;
        }
        free(g_aliases[idx].value);
        g_aliases[idx].value = value_copy;
        return 0;
    }

    if (g_alias_count >= MAX_ALIASES) {
        fprintf(stderr, "alias table is full\n");
        return 1;
    }

    name_copy = strdup(name);
    value_copy = strdup(value);
    if (name_copy == NULL || value_copy == NULL) {
        perror("strdup");
        free(name_copy);
        free(value_copy);
        return 1;
    }

    g_aliases[g_alias_count].name = name_copy;
    g_aliases[g_alias_count].value = value_copy;
    g_alias_count++;
    return 0;
}

static const char *get_alias_value(const char *name) {
    int idx = find_alias_index(name);
    if (idx < 0) {
        return NULL;
    }
    return g_aliases[idx].value;
}

static int apply_alias_if_needed(char *argv[], int *argc, char *buffer, size_t buffer_size) {
    const char *alias_value;
    int written;

    if (*argc <= 0) {
        return 0;
    }

    alias_value = get_alias_value(argv[0]);
    if (alias_value == NULL) {
        return 0;
    }

    written = snprintf(buffer, buffer_size, "%s", alias_value);
    if (written < 0 || (size_t)written >= buffer_size) {
        fprintf(stderr, "alias expansion too long\n");
        return -1;
    }

    for (int i = 1; i < *argc; ++i) {
        written = snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer), " %s", argv[i]);
        if (written < 0 || (size_t)written >= buffer_size - strlen(buffer)) {
            fprintf(stderr, "alias expansion too long\n");
            return -1;
        }
    }

    *argc = parse_line(buffer, argv);
    if (*argc < 0) {
        return -1;
    }

    return 1;
}

static void expand_environment_variables(int argc, char *argv[]) {
    for (int i = 0; i < argc; ++i) {
        if (argv[i][0] == '$' && argv[i][1] != '\0') {
            const char *value = getenv(argv[i] + 1);
            argv[i] = (char *)(value != NULL ? value : "");
        }
    }
}

static int is_builtin(char *cmd) {
    return strcmp(cmd, "exit") == 0 || strcmp(cmd, "cd") == 0 || strcmp(cmd, "pwd") == 0 ||
           strcmp(cmd, "history") == 0 || strcmp(cmd, "status") == 0 || strcmp(cmd, "help") == 0 ||
           strcmp(cmd, "export") == 0 || strcmp(cmd, "alias") == 0 || strcmp(cmd, "jobs") == 0 ||
           strcmp(cmd, "fg") == 0 || strcmp(cmd, "bg") == 0;
}

static int run_builtin(int argc, char *argv[], int *should_exit) {
    *should_exit = 0;

    if (argc == 0) {
        return 0;
    }

    if (strcmp(argv[0], "exit") == 0) {
        *should_exit = 1;
        return 0;
    }

    if (strcmp(argv[0], "cd") == 0) {
        const char *path = (argc > 1) ? argv[1] : getenv("HOME");
        if (path == NULL) {
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

    if (strcmp(argv[0], "history") == 0) {
        print_history();
        return 0;
    }

    if (strcmp(argv[0], "status") == 0) {
        printf("%d\n", g_last_status);
        return 0;
    }

    if (strcmp(argv[0], "help") == 0) {
        puts("builtins: cd pwd exit history status help export alias jobs fg bg");
        puts("features: quotes, < > >>, pipes, &, ;, &&, ||, !!, !n, !prefix, $VAR, .minishellrc");
        return 0;
    }

    if (strcmp(argv[0], "export") == 0) {
        char *eq;
        if (argc < 2) {
            fprintf(stderr, "export: usage export NAME=value\n");
            return 1;
        }

        eq = strchr(argv[1], '=');
        if (eq == NULL) {
            fprintf(stderr, "export: expected NAME=value\n");
            return 1;
        }

        *eq = '\0';
        if (setenv(argv[1], eq + 1, 1) == -1) {
            perror("setenv");
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[0], "alias") == 0) {
        if (argc == 1) {
            print_aliases();
            return 0;
        }

        {
            char *eq = strchr(argv[1], '=');
            if (eq != NULL) {
                char value[MAX_LINE];
                size_t len;

                *eq = '\0';

                snprintf(value, sizeof(value), "%s", eq + 1);
                for (int i = 2; i < argc; ++i) {
                    strncat(value, " ", sizeof(value) - strlen(value) - 1);
                    strncat(value, argv[i], sizeof(value) - strlen(value) - 1);
                }

                len = strlen(value);
                if (len >= 2 && ((value[0] == '"' && value[len - 1] == '"') ||
                                 (value[0] == '\'' && value[len - 1] == '\''))) {
                    value[len - 1] = '\0';
                    return set_alias(argv[1], value + 1);
                }

                return set_alias(argv[1], value);
            }
        }

        if (argc >= 3) {
            char value[MAX_LINE];
            value[0] = '\0';
            for (int i = 2; i < argc; ++i) {
                if (i > 2) {
                    strncat(value, " ", sizeof(value) - strlen(value) - 1);
                }
                strncat(value, argv[i], sizeof(value) - strlen(value) - 1);
            }
            return set_alias(argv[1], value);
        }

        fprintf(stderr, "alias: usage alias name=value or alias name value\n");
        return 1;
    }

    if (strcmp(argv[0], "jobs") == 0) {
        print_jobs();
        return 0;
    }

    if (strcmp(argv[0], "fg") == 0) {
        if (argc < 2) {
            fprintf(stderr, "fg: usage fg <job-id>\n");
            return 1;
        }
        return builtin_fg(argv[1]);
    }

    if (strcmp(argv[0], "bg") == 0) {
        if (argc < 2) {
            fprintf(stderr, "bg: usage bg <job-id>\n");
            return 1;
        }
        return builtin_bg(argv[1]);
    }

    return -1;
}

static int run_builtin_with_redirection(Command *cmd, int *should_exit) {
    int saved_stdin = -1;
    int saved_stdout = -1;
    int result;

    if (cmd->infile != NULL || cmd->outfile != NULL) {
        saved_stdin = dup(STDIN_FILENO);
        saved_stdout = dup(STDOUT_FILENO);
        if (saved_stdin == -1 || saved_stdout == -1) {
            perror("dup");
            if (saved_stdin != -1) {
                close(saved_stdin);
            }
            if (saved_stdout != -1) {
                close(saved_stdout);
            }
            *should_exit = 0;
            return 1;
        }

        if (apply_redirection(cmd->infile, cmd->outfile, cmd->append) != 0) {
            close(saved_stdin);
            close(saved_stdout);
            *should_exit = 0;
            return 1;
        }
    }

    result = run_builtin(cmd->argc, cmd->argv, should_exit);

    if (saved_stdin != -1) {
        if (dup2(saved_stdin, STDIN_FILENO) == -1) {
            perror("restore stdin");
        }
        close(saved_stdin);
    }

    if (saved_stdout != -1) {
        if (dup2(saved_stdout, STDOUT_FILENO) == -1) {
            perror("restore stdout");
        }
        close(saved_stdout);
    }

    return result;
}

static int run_external(Command *cmd, int background, const char *command_text) {
    int status;
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        setpgid(0, 0);
        signal(SIGINT, SIG_DFL);
        if (apply_redirection(cmd->infile, cmd->outfile, cmd->append) != 0) {
            _exit(1);
        }
        execvp(cmd->argv[0], cmd->argv);
        perror("execvp");
        _exit(127);
    }

    setpgid(pid, pid);

    if (background) {
        int job_id;
        pid_t pids[1];

        pids[0] = pid;
        job_id = add_job(pid, pids, 1, command_text);
        if (job_id < 0) {
            return 1;
        }
        printf("[%d] %ld\n", job_id, (long)pid);
        return 0;
    }

    g_foreground_pgid = pid;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        g_foreground_pgid = 0;
        return 1;
    }
    g_foreground_pgid = 0;

    return status_from_wait(status);
}

static int run_pipeline(Command cmds[], int cmd_count, int background, const char *command_text) {
    int pipes[MAX_CMDS - 1][2];
    pid_t pids[MAX_CMDS];
    pid_t pgid = 0;
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
            for (int k = 0; k < i; ++k) {
                waitpid(pids[k], NULL, 0);
            }
            return 1;
        }

        if (pids[i] == 0) {
            if (pgid == 0) {
                setpgid(0, 0);
            } else {
                setpgid(0, pgid);
            }
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

            if (apply_redirection(cmds[i].infile, cmds[i].outfile, cmds[i].append) != 0) {
                _exit(1);
            }

            execvp(cmds[i].argv[0], cmds[i].argv);
            perror("execvp");
            _exit(127);
        }

        if (pgid == 0) {
            pgid = pids[i];
        }
        setpgid(pids[i], pgid);
    }

    for (int i = 0; i < cmd_count - 1; ++i) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if (background) {
        int job_id = add_job(pgid, pids, cmd_count, command_text);
        if (job_id < 0) {
            return 1;
        }
        printf("[%d] %ld\n", job_id, (long)pgid);
        return 0;
    }

    g_foreground_pgid = pgid;
    for (int i = 0; i < cmd_count; ++i) {
        int status;
        if (waitpid(pids[i], &status, 0) == -1) {
            perror("waitpid");
            last_status = 1;
            continue;
        }
        if (i == cmd_count - 1) {
            last_status = status_from_wait(status);
        }
    }
    g_foreground_pgid = 0;

    return last_status;
}

static int execute_single_segment(char *segment, int *should_exit) {
    char *argv[MAX_ARGS];
    int argc;
    int background = 0;
    char alias_buffer[MAX_LINE];
    char command_snapshot[MAX_LINE];
    Command cmds[MAX_CMDS];
    int cmd_count;

    *should_exit = 0;

    snprintf(command_snapshot, sizeof(command_snapshot), "%s", segment);

    argc = parse_line(segment, argv);
    if (argc <= 0) {
        return (argc < 0) ? 1 : 0;
    }

    if (apply_alias_if_needed(argv, &argc, alias_buffer, sizeof(alias_buffer)) < 0) {
        return 1;
    }

    expand_environment_variables(argc, argv);

    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "&") == 0 && i != argc - 1) {
            fprintf(stderr, "'&' must appear at the end\n");
            return 1;
        }
    }

    if (strcmp(argv[argc - 1], "&") == 0) {
        background = 1;
        argv[--argc] = NULL;
    }

    if (argc == 0) {
        return 0;
    }

    if (parse_commands(argc, argv, cmds, &cmd_count) != 0) {
        return 1;
    }

    if (cmd_count == 1 && is_builtin(cmds[0].argv[0])) {
        int code;
        if (background) {
            fprintf(stderr, "background mode is not supported for builtin commands\n");
            return 1;
        }
        code = run_builtin_with_redirection(&cmds[0], should_exit);
        return (code < 0) ? 1 : code;
    }

    if (cmd_count == 1) {
        return run_external(&cmds[0], background, command_snapshot);
    }

    return run_pipeline(cmds, cmd_count, background, command_snapshot);
}

static int execute_line(char *line, int *should_exit) {
    char *segments[MAX_SEGMENTS];
    int ops[MAX_SEGMENTS];
    int seg_count;
    int last_status = 0;

    *should_exit = 0;

    seg_count = split_chained_commands(line, segments, ops);
    if (seg_count < 0) {
        return 1;
    }

    for (int i = 0; i < seg_count; ++i) {
        int run_now = 1;

        if (segments[i][0] == '\0') {
            continue;
        }

        if (i > 0) {
            if (ops[i] == OP_AND && last_status != 0) {
                run_now = 0;
            }
            if (ops[i] == OP_OR && last_status == 0) {
                run_now = 0;
            }
        }

        if (!run_now) {
            continue;
        }

        last_status = execute_single_segment(segments[i], should_exit);
        if (*should_exit) {
            break;
        }
    }

    return last_status;
}

static void load_startup_rc(void) {
    const char *candidates[2] = {".minishellrc", NULL};
    char home_rc[PATH_MAX];

    {
        const char *home = getenv("HOME");
        if (home != NULL && snprintf(home_rc, sizeof(home_rc), "%s/.minishellrc", home) < (int)sizeof(home_rc)) {
            candidates[1] = home_rc;
        }
    }

    for (int i = 0; i < 2; ++i) {
        FILE *fp;
        char *line = NULL;
        size_t cap = 0;

        if (candidates[i] == NULL) {
            continue;
        }

        fp = fopen(candidates[i], "r");
        if (fp == NULL) {
            continue;
        }

        while (getline(&line, &cap, fp) != -1) {
            char *content;
            int should_exit = 0;
            int code;

            trim_newline(line);
            content = trim_spaces(line);

            if (content[0] == '\0' || content[0] == '#') {
                continue;
            }

            {
                char *exec_copy = strdup(content);
                if (exec_copy == NULL) {
                    perror("strdup");
                    continue;
                }
                code = execute_line(exec_copy, &should_exit);
                free(exec_copy);
                (void)code;
            }

            if (should_exit) {
                break;
            }
        }

        free(line);
        fclose(fp);
    }
}

static void cleanup_state(void) {
    for (int i = 0; i < g_history_count; ++i) {
        free(g_history[i]);
    }

    for (int i = 0; i < g_alias_count; ++i) {
        free(g_aliases[i].name);
        free(g_aliases[i].value);
    }

    for (int i = 0; i < g_job_count; ++i) {
        free(g_jobs[i].command);
    }
}

int main(void) {
    char *line = NULL;
    size_t cap = 0;
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    load_startup_rc();

    while (1) {
        printf("mini-shell$ ");
        fflush(stdout);

        if (getline(&line, &cap, stdin) == -1) {
            if (feof(stdin)) {
                putchar('\n');
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            perror("getline");
            break;
        }

        reap_background_jobs();

        {
            char *command_text;
            int should_exit = 0;

            trim_newline(line);
            command_text = trim_spaces(line);
            if (command_text[0] == '\0') {
                continue;
            }

            if (command_text[0] == '!' && command_text[1] != '\0') {
                const char *resolved = NULL;

                if (resolve_history_reference(command_text, &resolved) != 0) {
                    g_last_status = 1;
                    continue;
                }

                command_text = (char *)resolved;
                printf("%s\n", command_text);
            }

            add_history(command_text);

            {
                char *exec_copy = strdup(command_text);
                if (exec_copy == NULL) {
                    perror("strdup");
                    g_last_status = 1;
                    continue;
                }

                g_last_status = execute_line(exec_copy, &should_exit);
                free(exec_copy);
            }

            if (should_exit) {
                break;
            }
        }
    }

    cleanup_state();
    free(line);
    return 0;
}
