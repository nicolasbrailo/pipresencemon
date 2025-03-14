#include "occupancy_commands.h"
#include "cfg.h"

#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
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
  // pid is atomic so the signal handler can reset it on crash
  atomic_int pid;
  bool should_restart_on_crash;
  bool should_run_now;
  // Cooldown before restarting a crashed process
  size_t restart_cmd_wait_time_seconds;
  size_t restart_count;
  size_t max_restarts;
};

enum CurrentState {
  STATE_INVALID,
  STATE_OCCUPIED,
  STATE_VACANT,
};

struct OccupancyCommands {
  enum CurrentState current_state;
  size_t restart_cmd_wait_time_seconds;
  size_t crash_on_repeated_cmd_failure_count;

  size_t on_occupancy_cmds_cnt;
  struct OccupancyTransitionCommand *on_occupancy_cmds;

  size_t on_vacancy_cmds_cnt;
  struct OccupancyTransitionCommand *on_vacancy_cmds;

  // A ptr to the config is held for callbacks, only while init runs
  const struct PiPresenceMonConfig *cfg;
};

// Non null if the SIGCHLD handler has been set
struct OccupancyCommands *g_sigchld_handler = NULL;

static size_t count_argc(const char *cmd) {
  size_t argc = 0;
  for (size_t i = 0; cmd[i] != '\0'; ++i) {
    if (cmd[i] == ' ')
      ++argc;
  }
  return argc;
}

static bool parse_transition_cmd_from_cfg(struct CommandConfig *cmdcfg,
                                          struct OccupancyTransitionCommand *cmd_state) {
  cmd_state->pid = 0;
  cmd_state->should_restart_on_crash = cmdcfg->should_restart_on_crash;
  cmd_state->restart_count = 0;
  cmd_state->max_restarts = cmdcfg->max_restarts;
  cmd_state->should_run_now = false;
  cmd_state->cmd = malloc((1 + strlen(cmdcfg->cmd)) * sizeof(char));

  if (!cmd_state->cmd)
    goto ALLOC_ERR;
  strcpy(cmd_state->cmd, cmdcfg->cmd);

  // Reserve argc+2: $BIN [$ARG_ARR] \0
  const size_t argc = count_argc(cmd_state->cmd) + 2;
  cmd_state->args = malloc(argc * sizeof(void *));
  if (!cmd_state->args)
    goto ALLOC_ERR;

  {
    size_t i = 0;
    char *tok = strtok(cmd_state->cmd, " ");
    cmd_state->bin = &cmd_state->cmd[0];
    while (tok != NULL) {
      cmd_state->args[i++] = tok;
      tok = strtok(NULL, " ");
    }
  }

  cmd_state->args[argc - 1] = NULL;

  return true;

ALLOC_ERR:
  fprintf(stderr, "occupancy_commands_init bad alloc parsing command\n");
  free(cmd_state->cmd);
  free(cmd_state->args);
  return false;
}

static void launch_commands(size_t sz, struct OccupancyTransitionCommand *cmds,
                            struct OccupancyCommands *self, bool respawn) {
  for (size_t cmd_i = 0; cmd_i < sz; ++cmd_i) {
    if (cmds[cmd_i].pid != 0) {
      if (respawn) {
        // We're respawning crashed processes, ignore if child already running
        continue;
      } else {
        printf("Error launching command %s: already launched with pid %d\n", cmds[cmd_i].bin,
               cmds[cmd_i].pid);
        printf("Will ignore further commands");
        return;
      }
    }

    if (respawn && !cmds[cmd_i].should_restart_on_crash) {
      // Command exited or crashed, but doesn't want to be restarted
      continue;
    }

    if (respawn && cmds[cmd_i].restart_cmd_wait_time_seconds > 0) {
      cmds[cmd_i].restart_cmd_wait_time_seconds -= 1;
      continue;
    }

    if (respawn) {
      cmds[cmd_i].restart_count++;
      printf("Restarting (attempt #%zu) ambience app:", cmds[cmd_i].restart_count);
    } else {
      printf("Launching ambience app %zu:\n", cmd_i);
    }

    printf("\t");
    for (size_t i = 0; cmds[cmd_i].args[i]; ++i) {
      printf(" %s", cmds[cmd_i].args[i]);
    }
    printf("\n");

    if (respawn && self->crash_on_repeated_cmd_failure_count > 0 &&
        cmds[cmd_i].restart_count > self->crash_on_repeated_cmd_failure_count) {
      printf("Restart attempts (%zu) over retry limit, something is broken and will crash now\n",
             cmds[cmd_i].restart_count);
      abort();
    }

    cmds[cmd_i].pid = fork();
    cmds[cmd_i].should_run_now = true;
    if (cmds[cmd_i].pid == 0) {
      // Wayfire crashes if the monitor switches on or off too quickly, so we give it a bit of time
      printf("Sleep 1 before execv\n");
      sleep(1);
      execvp(cmds[cmd_i].bin, cmds[cmd_i].args);
      perror("Background task failed to execve");
      abort();
    } else if (cmds[cmd_i].pid < 0) {
      perror("Failed to launch background task");
      cmds[cmd_i].pid = 0;
    } else {
      cmds[cmd_i].restart_cmd_wait_time_seconds = self->restart_cmd_wait_time_seconds;
    }
  }
}

static void stop_commands(size_t sz, struct OccupancyTransitionCommand *cmds) {
  for (size_t cmd_i = 0; cmd_i < sz; ++cmd_i) {
    if (!cmds[cmd_i].should_run_now && cmds[cmd_i].pid == 0) {
      continue;
    }

    if (cmds[cmd_i].should_run_now && cmds[cmd_i].pid == 0) {
      printf("Warning: try stopping command %s, but is not running\n", cmds[cmd_i].bin);
      continue;
    }

    cmds[cmd_i].should_run_now = false;

    printf("Stopping:");
    for (size_t i = 0; cmds[cmd_i].args[i]; ++i) {
      printf(" %s", cmds[cmd_i].args[i]);
    }
    printf("\n");

    if (kill(cmds[cmd_i].pid, SIGINT) != 0) {
      perror("Failed to stop background task, try to kill");
      if (kill(cmds[cmd_i].pid, SIGKILL) != 0) {
        perror("Failed to kill background task");
        // If this fails, pid will be non zero, so a new one won't be launched
        // Probably better to avoid launching new ambience apps, instead of leaking them
        return;
      }
    }

    int wstatus;
    waitpid(cmds[cmd_i].pid, &wstatus, 0);
    cmds[cmd_i].pid = 0;

    if (wstatus != 0) {
      printf("ERROR: Background task %s exit with status %d\n", cmds[cmd_i].bin, wstatus);
    }
  }
}

bool sighandler_search_exit_child(size_t sz, struct OccupancyTransitionCommand *cmds, int pid,
                                  int wstatus) {
  for (size_t i = 0; i < sz; ++i) {
    if (pid == cmds[i].pid) {
      if (cmds[i].should_run_now) {
        if (wstatus == 0) {
          printf("Command %s with pid %i exit normally\n", cmds[i].bin, pid);
          cmds[i].should_run_now = false;
        } else {
          printf("CRASH: Command %s with pid %i exit, ret %i\n", cmds[i].bin, pid, wstatus);
          if (cmds[i].should_restart_on_crash) {
            printf("Will restart in %zu seconds...\n", cmds[i].restart_cmd_wait_time_seconds);
          } else {
            printf("This app WON'T restart.\n");
          }
        }
      } else {
        printf("Command %s with pid %i exit, ret %i\n", cmds[i].bin, pid, wstatus);
      }
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

    bool found =
        sighandler_search_exit_child(g_sigchld_handler->on_occupancy_cmds_cnt,
                                     g_sigchld_handler->on_occupancy_cmds, exitedpid, wstatus);
    if (!found) {
      sighandler_search_exit_child(g_sigchld_handler->on_vacancy_cmds_cnt,
                                   g_sigchld_handler->on_vacancy_cmds, exitedpid, wstatus);
    }
    if (!found) {
      printf("Error: received SIGCHLD for unknown child with pid %i\n", exitedpid);
    }
  }
}

struct OccupancyCommands *occupancy_commands_init(const struct PiPresenceMonConfig *cfg) {
  if (g_sigchld_handler != NULL) {
    fprintf(stderr, "Handler for occupancy command exit already set. Are you creating two "
                    "OccupancyCommands object?\n");
    return NULL;
  }

  struct OccupancyCommands *self = malloc(sizeof(struct OccupancyCommands));
  if (!self) {
    fprintf(stderr, "occupancy_commands_init bad alloc\n");
    goto ERR;
  }

  self->current_state = STATE_INVALID;
  self->cfg = cfg;
  self->restart_cmd_wait_time_seconds = cfg->restart_cmd_wait_time_seconds;
  self->crash_on_repeated_cmd_failure_count = cfg->crash_on_repeated_cmd_failure_count;

  self->on_occupancy_cmds_cnt = cfg->on_occupancy_sz;
  self->on_occupancy_cmds =
      malloc(self->on_occupancy_cmds_cnt * sizeof(struct OccupancyTransitionCommand));
  memset(self->on_occupancy_cmds, 0,
         self->on_occupancy_cmds_cnt * sizeof(struct OccupancyTransitionCommand));

  self->on_vacancy_cmds_cnt = cfg->on_vacancy_sz;
  self->on_vacancy_cmds =
      malloc(self->on_vacancy_cmds_cnt * sizeof(struct OccupancyTransitionCommand));
  memset(self->on_vacancy_cmds, 0,
         self->on_vacancy_cmds_cnt * sizeof(struct OccupancyTransitionCommand));

  if (!self->on_occupancy_cmds || !self->on_vacancy_cmds) {
    fprintf(stderr, "occupancy_commands_init on_vacancy_cmds bad alloc\n");
    goto ERR;
  }

  printf("OccupancyCommands starting. On occupancy, will:\n");
  for (size_t i = 0; i < cfg->on_occupancy_sz; ++i) {
    struct OccupancyTransitionCommand *cmd_cfg = &self->on_occupancy_cmds[i];
    const bool ok = parse_transition_cmd_from_cfg(&cfg->on_occupancy[i], cmd_cfg);
    if (!ok) {
      goto ERR;
    }
    printf(" * exec `%s`\n", cmd_cfg->cmd);
  }

  printf("On vacancy, will:\n");
  for (size_t i = 0; i < cfg->on_vacancy_sz; ++i) {
    struct OccupancyTransitionCommand *cmd_cfg = &self->on_vacancy_cmds[i];
    const bool ok = parse_transition_cmd_from_cfg(&cfg->on_vacancy[i], cmd_cfg);
    if (!ok) {
      goto ERR;
    }
    printf(" * exec `%s`\n", cmd_cfg->cmd);
  }

  // Nothing else in here should access the config struct
  self->cfg = NULL;

  // Set up sighandler
  g_sigchld_handler = self;
  signal(SIGCHLD, sighandler_on_child_cmd_exit);

  return self;
ERR:
  occupancy_commands_free(self);
  return NULL;
}

void occupancy_commands_free(struct OccupancyCommands *self) {
  if (!self) {
    return;
  }

  if (self->current_state != STATE_INVALID) {
    stop_commands(self->on_occupancy_cmds_cnt, self->on_occupancy_cmds);
    stop_commands(self->on_vacancy_cmds_cnt, self->on_vacancy_cmds);
  }

  if (self->on_occupancy_cmds) {
    for (size_t i = 0; i < self->on_occupancy_cmds_cnt; ++i) {
      free(self->on_occupancy_cmds[i].cmd);
      free(self->on_occupancy_cmds[i].args);
    }
    free(self->on_occupancy_cmds);
  }

  if (self->on_vacancy_cmds) {
    for (size_t i = 0; i < self->on_vacancy_cmds_cnt; ++i) {
      free(self->on_vacancy_cmds[i].cmd);
      free(self->on_vacancy_cmds[i].args);
    }
    free(self->on_vacancy_cmds);
  }

  free(self);
}

void occupancy_commands_on_occupancy(struct OccupancyCommands *self) {
  if (self->current_state == STATE_OCCUPIED) {
    printf("Occupancy commands error: tried to set state to OCCUPIED while already in OCCUPIED "
           "state\n");
    return;
  }

  self->current_state = STATE_OCCUPIED;
  stop_commands(self->on_vacancy_cmds_cnt, self->on_vacancy_cmds);
  launch_commands(self->on_occupancy_cmds_cnt, self->on_occupancy_cmds, self, false);
}

void occupancy_commands_on_vacancy(struct OccupancyCommands *self) {
  if (self->current_state == STATE_VACANT) {
    printf(
        "Occupancy commands error: tried to set state to VACANT while already in VACANT state\n");
    return;
  }

  self->current_state = STATE_VACANT;
  stop_commands(self->on_occupancy_cmds_cnt, self->on_occupancy_cmds);
  launch_commands(self->on_vacancy_cmds_cnt, self->on_vacancy_cmds, self, false);
}

void occupancy_commands_tick(struct OccupancyCommands *self) {
  if (self->current_state == STATE_OCCUPIED) {
    launch_commands(self->on_occupancy_cmds_cnt, self->on_occupancy_cmds, self, true);
  } else if (self->current_state == STATE_VACANT) {
    launch_commands(self->on_vacancy_cmds_cnt, self->on_vacancy_cmds, self, true);
  }
}
