#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LINE 512
#define MAX_SERVICES 128

typedef struct {
  char command[MAX_LINE];
  char *argv[32];
  pid_t pid;
  int respawn;
} service;

static service g_services[MAX_SERVICES];
static int g_count = 0;
static int g_shutdown = 0;

static void handle_signal(int sig) {
  if (sig == SIGINT || sig == SIGTERM)
    g_shutdown = 1;
}

static void trim(char *s) {
  size_t n = strlen(s);
  while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' ||
               s[n - 1] == '\t'))
    s[--n] = '\0';
  while (*s == ' ' || *s == '\t')
    s++;
}

static int parse(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  char line[MAX_LINE];
  int respawn = 0;

  while (fgets(line, sizeof(line), f)) {
    trim(line);
    if (!*line || *line == '#')
      continue;
    if (g_count >= MAX_SERVICES)
      break;

    char *p = strchr(line, '#');
    if (p)
      *p = '\0';
    trim(line);
    if (!*line)
      continue;

    if (strncmp(line, "respawn:", 8) == 0) {
      respawn = 1;
      memmove(line, line + 8, strlen(line + 8) + 1);
      trim(line);
    } else if (strncmp(line, "wait:", 5) == 0) {
      respawn = 0;
      memmove(line, line + 5, strlen(line + 5) + 1);
      trim(line);
    }

    if (*line != '/')
      continue;

    char *args = strchr(line, ' ');
    if (args)
      *args++ = '\0';

    strncpy(g_services[g_count].command, line, MAX_LINE);
    g_services[g_count].argv[0] = g_services[g_count].command;
    int argc = 1;
    if (args) {
      char *tok = strtok(args, " ");
      while (tok && argc < 31) {
        g_services[g_count].argv[argc++] = tok;
        tok = strtok(NULL, " ");
      }
    }
    g_services[g_count].argv[argc] = NULL;
    g_services[g_count].respawn = respawn;
    g_services[g_count].pid = 0;
    g_count++;
    respawn = 0;
  }

  fclose(f);
  return 0;
}

static void spawn(service *s) {
  pid_t pid = fork();
  if (pid < 0)
    return;
  if (pid == 0) {
    setsid();
    execv(s->command, s->argv);
    _exit(127);
  }
  s->pid = pid;
}

static void start_all(void) {
  for (int i = 0; i < g_count; i++)
    spawn(&g_services[i]);
}

static void reap(void) {
  int st;
  pid_t pid;
  while ((pid = waitpid(-1, &st, WNOHANG)) > 0) {
    for (int i = 0; i < g_count; i++) {
      if (g_services[i].pid == pid) {
        g_services[i].pid = 0;
        if (g_services[i].respawn)
          spawn(&g_services[i]);
        break;
      }
    }
  }
}

static void shutdown_all(void) {
  for (int i = 0; i < g_count; i++)
    if (g_services[i].pid)
      kill(g_services[i].pid, SIGTERM);
  for (int i = 0; i < 30 && g_count; i++) {
    int done = 1;
    for (int j = 0; j < g_count; j++)
      if (g_services[j].pid && waitpid(g_services[j].pid, NULL, WNOHANG) <= 0)
        done = 0;
    if (done)
      break;
    usleep(100000);
  }
  for (int i = 0; i < g_count; i++) {
    if (g_services[i].pid) {
      kill(g_services[i].pid, SIGKILL);
      waitpid(g_services[i].pid, NULL, 0);
    }
  }
}

int main(int argc, char *argv[]) {
  const char *init_file = "/etc/init";

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      printf("Usage: %s [-f config]\n", argv[0]);
      return 0;
    } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
      printf("minit 0.1.0\n");
      return 0;
    } else if (!strcmp(argv[i], "-f") && i + 1 < argc) {
      init_file = argv[++i];
    }
  }

  int test = getenv("MINIT_TEST") != NULL;

  if (getpid() != 1 && !test) {
    fprintf(stderr, "minit: must run as PID 1\n");
    return 1;
  }

  if (!test) {
    setsid();
    umask(0);
    if (chdir("/") != 0) {
    }
    close(0);
    close(1);
    close(2);
    int nfd = open("/dev/null", O_RDWR);
    if (nfd >= 0) {
      dup2(nfd, 0);
      dup2(nfd, 1);
      dup2(nfd, 2);
      close(nfd);
    }
    FILE *f = fopen("/var/run/minit.pid", "w");
    if (f) {
      fprintf(f, "%d\n", getpid());
      fclose(f);
    }
  }

  if (parse(init_file) < 0) {
    fprintf(stderr, "minit: cannot parse %s\n", init_file);
    return 1;
  }

  struct sigaction sa = {
      .sa_handler = handle_signal, .sa_flags = SA_RESTART, .sa_mask = {}};
  sigaction(SIGCHLD, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  signal(SIGPIPE, SIG_IGN);

  if (!test)
    fprintf(stderr, "minit: starting %d services\n", g_count);

  start_all();

  while (!g_shutdown) {
    pause();
    reap();
  }

  shutdown_all();
  sync();
  return 0;
}