#include "ambience_mode.h"

#include "cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

struct AmbienceModeCfg {
  char *cmd;
  char *bin;
  char **args;
  pid_t pid;
};

struct AmbienceModeCfg *ambience_mode_init(void *fcfg) {
  struct AmbienceModeCfg *cfg = malloc(sizeof(struct AmbienceModeCfg));
  if (!cfg) {
    fprintf(stderr, "ambience_mode_init bad alloc\n");
    return NULL;
  }

  cfg->pid = 0;
  cfg->cmd = NULL;
  const bool cfg_read = cfg_read_str(fcfg, "ambience_mode_command", &cfg->cmd);

  if (!cfg_read) {
    fprintf(stderr, "Can't find ambience_mode_command\n");
    ambience_mode_free(cfg);
    return NULL;
  }

  size_t argc = 0;
  for (size_t i = 0; cfg->cmd[i] != '\0'; ++i) {
    if (cfg->cmd[i] == ' ')
      ++argc;
  }
  {
    cfg->args = malloc((argc + 1) * sizeof(void *));
    cfg->args[argc] = NULL;
    size_t i = 0;
    char *tok = strtok(cfg->cmd, " ");
    cfg->bin = cfg->cmd;
    while (tok != NULL) {
      if (i > 0) {
        cfg->args[i - 1] = tok;
      }

      ++i;
      tok = strtok(NULL, " ");
    }
  }

  return cfg;
}

void ambience_mode_free(struct AmbienceModeCfg *cfg) {
  if (!cfg) {
    return;
  }

  if (cfg->cmd) {
    free(cfg->cmd);
  }

  free(cfg);
}

void ambience_mode_enter(struct AmbienceModeCfg *cfg) {
  if (cfg->pid != 0) {
    return;
  }

  printf("Launching ambience app: %s ", cfg->cmd);
  size_t i = 0;
  while (cfg->args[i]) {
    printf(" %s", cfg->args[i++]);
  }
  printf("\n");

  cfg->pid = fork();
  if (cfg->pid == 0) {
    execvp(cfg->bin, cfg->args);
    perror("Ambience mode app failed to execve");
  } else if (cfg->pid < 0) {
    perror("Failed to launch ambience mode");
    cfg->pid = 0;
  }
}

void ambience_mode_leave(struct AmbienceModeCfg *cfg) {
  if (cfg->pid == 0) {
    return;
  }

  printf("Stopping ambience app...\n");
  if (kill(cfg->pid, SIGINT) != 0) {
    perror("Failed to stop ambience mode, try to kill");
    if (kill(cfg->pid, SIGKILL) != 0) {
      perror("Failed to kill ambience mode");
      // If this fails, pid will be non zero, so a new one won't be launched
      // Probably better to avoid launching new ambience apps, instead of leaking them
      return;
    }
  }

  // TODO monitor wait ret
  int wstatus;
  waitpid(cfg->pid, &wstatus, 0);
  cfg->pid = 0;
}
