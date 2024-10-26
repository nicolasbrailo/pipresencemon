#include "occupancy_commands.h"
#include "cfg.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

struct OccupancyTransitionCommand {
  // Copy of config string (eg "echo one two three")
  // This string will get used by strtok, so it's not printable. We keep it to
  // free the memory later.
  char *cmd;
  // Pointer to first workd in config string (eg ptr to "echo")
  char *bin;
  // Array of ptrs to args (eg "one two three")
  char **args;
  pid_t pid;
};

enum CurrentState {
    STATE_INVALID,
    STATE_OCCUPIED,
    STATE_VACANT,
};

struct OccupancyCommands {
    enum CurrentState current_state;

    size_t on_occupancy_cmds_read;
    size_t on_occupancy_cmds_cnt;
    struct OccupancyTransitionCommand* on_occupancy_cmds;

    size_t on_vacancy_cmds_read;
    size_t on_vacancy_cmds_cnt;
    struct OccupancyTransitionCommand* on_vacancy_cmds;
};

// Non null if the SIGCHLD handler has been set
struct OccupancyCommands* g_sigchld_handler = NULL;


static size_t count_argc(const char* cmd) {
  size_t argc = 0;
  for (size_t i = 0; cmd[i] != '\0'; ++i) {
    if (cmd[i] == ' ')
      ++argc;
  }
  return argc;
}

static bool parse_transition_cmd_from_cfg(const char* cmd, struct OccupancyTransitionCommand* cmd_cfg) {
  cmd_cfg->pid = 0;
  cmd_cfg->cmd = malloc((1 + strlen(cmd)) * sizeof(char));
  if (!cmd_cfg->cmd) goto ALLOC_ERR;
  strcpy(cmd_cfg->cmd, cmd);

  const size_t argc = count_argc(cmd_cfg->cmd);
  cmd_cfg->args = malloc((argc + 1) * sizeof(void *));
  if (!cmd_cfg->args) goto ALLOC_ERR;
  cmd_cfg->args[argc] = NULL; 

  {
    size_t i = 0;
    char *tok = strtok(cmd_cfg->cmd, " ");
    cmd_cfg->bin = &cmd_cfg->cmd[0];
    while (tok != NULL) {
      cmd_cfg->args[i++] = tok;
      tok = strtok(NULL, " ");
    }
  }

  return true;

ALLOC_ERR:
  fprintf(stderr, "occupancy_commands_init bad alloc parsing command\n");
  free(cmd_cfg->cmd);
  free(cmd_cfg->args);
  return false;
}

static void parse_vacancy_cmds_from_cfg(void *usr, const char* cmd) {
  struct OccupancyCommands *self = usr;
  struct OccupancyTransitionCommand * cmd_cfg = &self->on_vacancy_cmds[self->on_vacancy_cmds_read];
  const bool parse_ok = parse_transition_cmd_from_cfg(cmd, cmd_cfg);
  // On fail, don't increase counter. This will make the ambience mode validation init fail later
  if (parse_ok) self->on_vacancy_cmds_read++;
}

static void parse_occupancy_cmds_from_cfg(void* usr, const char* cmd) {
  struct OccupancyCommands *self = usr;
  struct OccupancyTransitionCommand* cmd_cfg = &self->on_occupancy_cmds[self->on_occupancy_cmds_read];
  const bool parse_ok = parse_transition_cmd_from_cfg(cmd, cmd_cfg);
  // On fail, don't increase counter. This will make the ambience mode validation init fail later
  if (parse_ok) self->on_occupancy_cmds_read++;
}

static void launch_commands(size_t sz, struct OccupancyTransitionCommand* cmds) {
  for (size_t cmd_i=0; cmd_i < sz; ++cmd_i) {
    if (cmds[cmd_i].pid != 0) {
      printf("Error launching command %s: already launched with pid %d\n", cmds[cmd_i].bin, cmds[cmd_i].pid);
      printf("Will ignore further commands");
      return;
    }

    printf("Launching ambience app: %s ", cmds[cmd_i].bin);
    for (size_t i = 0; cmds[cmd_i].args[i]; ++i) {
      printf(" %s", cmds[cmd_i].args[i++]);
    }
    printf("\n");

    cmds[cmd_i].pid = fork();
    if (cmds[cmd_i].pid == 0) {
      execvp(cmds[cmd_i].bin, cmds[cmd_i].args);
      perror("Ambience mode app failed to execve");
      abort();
    } else if (cmds[cmd_i].pid < 0) {
      perror("Failed to launch ambience mode app");
      cmds[cmd_i].pid = 0;
    }
  }
}

static void stop_commands(size_t sz, struct OccupancyTransitionCommand* cmds) {
  for (size_t cmd_i=0; cmd_i < sz; ++cmd_i) {
    if (cmds[cmd_i].pid == 0) {
      printf("Warning: try stopping command %s, but is not running\n", cmds[cmd_i].bin);
      continue;
    }

    printf("Stopping ambience app: %s ", cmds[cmd_i].bin);
    for (size_t i = 0; cmds[cmd_i].args[i]; ++i) {
      printf(" %s", cmds[cmd_i].args[i++]);
    }
    printf("\n");

    if (kill(cmds[cmd_i].pid, SIGINT) != 0) {
      perror("Failed to stop ambience mode, try to kill");
      if (kill(cmds[cmd_i].pid, SIGKILL) != 0) {
        perror("Failed to kill ambience mode");
        // If this fails, pid will be non zero, so a new one won't be launched
        // Probably better to avoid launching new ambience apps, instead of leaking them
        return;
      }
    }

    int wstatus;
    waitpid(cmds[cmd_i].pid, &wstatus, 0);
    cmds[cmd_i].pid = 0;

    if (wstatus != 0) {
      printf("ERROR: Ambience app %s exit with status %d\n", cmds[cmd_i].bin, wstatus);
    }
  }
}

bool sighandler_search_exit_child(size_t sz, struct OccupancyTransitionCommand* cmds, int pid, int wstatus) {
  for (size_t i=0; i < sz; ++i) {
    if (pid == cmds[i].pid) {
      printf("Command %s with pid %i exit, ret %i\n", cmds[i].bin, pid, wstatus);
      printf("TODO: RELAUNCH\n"); // XXX TODO
      cmds[i].pid = 0;
      return true;
    }
  }
  return false;
}

void sighandler_on_child_cmd_exit() {
    while (true) {
        int wstatus;
        int exitedpid = waitpid(-1, &wstatus, WNOHANG);
        if (exitedpid == 0) {
            break;
        } else if (exitedpid < 0 && errno == 10) {
            // No more pids
            break;
        } else if (exitedpid < 0) {
           perror("Error waitpid on signal handler");
           break;
        }

        bool found = sighandler_search_exit_child(g_sigchld_handler->on_occupancy_cmds_cnt, g_sigchld_handler->on_occupancy_cmds, exitedpid, wstatus);
        if (!found) {
            sighandler_search_exit_child(g_sigchld_handler->on_vacancy_cmds_cnt, g_sigchld_handler->on_vacancy_cmds, exitedpid, wstatus);
        }
        if (!found) {
            printf("Error: received SIGCHLD for unknown child with pid %i\n", exitedpid);
        }
    }
}


struct OccupancyCommands *occupancy_commands_init(const struct Config *cfg) {
  if (g_sigchld_handler != NULL) {
    fprintf(stderr, "Handler for occupancy command exit already set. Are you creating two OccupancyCommands object?\n");
    return NULL;
  }

  struct OccupancyCommands *self = malloc(sizeof(struct OccupancyCommands));
  if (!self) {
    fprintf(stderr, "occupancy_commands_init bad alloc\n");
    goto ERR;
  }

  self->current_state = STATE_INVALID;

  self->on_occupancy_cmds_cnt = cfg->on_occupancy_cmds_cnt;
  self->on_occupancy_cmds_read = 0;
  self->on_occupancy_cmds = malloc(self->on_occupancy_cmds_cnt * sizeof(struct OccupancyTransitionCommand));
  memset(self->on_occupancy_cmds, 0, self->on_occupancy_cmds_cnt * sizeof(struct OccupancyTransitionCommand));

  self->on_vacancy_cmds_cnt = cfg->on_vacancy_cmds_cnt;
  self->on_vacancy_cmds_read = 0;
  self->on_vacancy_cmds = malloc(self->on_vacancy_cmds_cnt * sizeof(struct OccupancyTransitionCommand));
  memset(self->on_vacancy_cmds, 0, self->on_vacancy_cmds_cnt * sizeof(struct OccupancyTransitionCommand));

  if (!self->on_occupancy_cmds || !self->on_vacancy_cmds) {
    fprintf(stderr, "occupancy_commands_init on_vacancy_cmds bad alloc\n");
    goto ERR;
  }

  cfg_each_cmd(cfg->on_occupancy_cmds, parse_occupancy_cmds_from_cfg, self);
  cfg_each_cmd(cfg->on_vacancy_cmds, parse_vacancy_cmds_from_cfg, self);

  if (self->on_occupancy_cmds_read != self->on_occupancy_cmds_cnt) {
    fprintf(stderr, "occupancy commands: read %zu commands, expected %zu\n", self->on_occupancy_cmds_read, self->on_occupancy_cmds_cnt);
    goto ERR;
  }

  if (self->on_vacancy_cmds_read != self->on_vacancy_cmds_cnt) {
    fprintf(stderr, "Vacancy commands: read %zu commands, expected %zu\n", self->on_vacancy_cmds_read, self->on_vacancy_cmds_cnt);
    goto ERR;
  }

  g_sigchld_handler = self;
  signal(SIGCHLD, sighandler_on_child_cmd_exit);
  return self;
ERR:
    occupancy_commands_free(self);
    return NULL;
}

void occupancy_commands_free(struct OccupancyCommands* self) {
  if (!self) {
    return;
  }

  if (self->current_state != STATE_INVALID) {
    stop_commands(self->on_occupancy_cmds_cnt, self->on_occupancy_cmds);
    stop_commands(self->on_vacancy_cmds_cnt, self->on_vacancy_cmds);
  }

  if (self->on_occupancy_cmds) {
      for (size_t i=0; i < self->on_occupancy_cmds_cnt; ++i) {
          free(self->on_occupancy_cmds[i].cmd);
          free(self->on_occupancy_cmds[i].args);
      }
      free(self->on_occupancy_cmds);
  }

  if (self->on_vacancy_cmds) {
      for (size_t i=0; i < self->on_vacancy_cmds_cnt; ++i) {
          free(self->on_vacancy_cmds[i].cmd);
          free(self->on_vacancy_cmds[i].args);
      }
      free(self->on_vacancy_cmds);
  }

  free(self);
}

void occupancy_commands_on_occupancy(struct OccupancyCommands* self) {
    if (self->current_state == STATE_OCCUPIED) {
        printf("Occupancy commands error: tried to set state to OCCUPIED while already in OCCUPIED state\n");
        return;
    }

    self->current_state = STATE_OCCUPIED;
    stop_commands(self->on_vacancy_cmds_cnt, self->on_vacancy_cmds);
    launch_commands(self->on_occupancy_cmds_cnt, self->on_occupancy_cmds);
}

void occupancy_commands_on_vacancy(struct OccupancyCommands* self) {
    if (self->current_state == STATE_VACANT) {
        printf("Occupancy commands error: tried to set state to VACANT while already in VACANT state\n");
        return;
    }

    self->current_state = STATE_VACANT;
    stop_commands(self->on_occupancy_cmds_cnt, self->on_occupancy_cmds);
    launch_commands(self->on_vacancy_cmds_cnt, self->on_vacancy_cmds);
}

