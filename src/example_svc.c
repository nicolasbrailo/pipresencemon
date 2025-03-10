#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

atomic_bool gUsrStop = false;
void sighandler(int sign) { gUsrStop = true; }

int main(int argc, const char **argv) {
  signal(SIGINT, sighandler);
  size_t cnt = 0;
  while (!gUsrStop) {
    printf("HELLO FROM SAMPLE SVC %s %s - CNT %zu\n", argv[0], argv[1], cnt++);
    sleep(1);
  }
  return 0;
}
