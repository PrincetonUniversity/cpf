#include<signal.h>
#include<setjmp.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/types.h>

#include "resteer.h"

#define RESTEER SIGRTMAX

static jmp_buf env;

static void handler(int sigNum) {
  (void) sigNum;
  longjmp(env, 1);
}

void enableResteer(RecoverFun recover, ContFun cont) {
  if(setjmp(env)) {
    recover();
  } else {
    struct sigaction action;
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    action.sa_restorer = NULL;
    if(sigaction(RESTEER, &action, NULL) == -1) {
      perror(__FILE__ "enableResteer(): ");
    }
  }

  cont();
}

void resteer(pid_t pid) {
  if(kill(pid, RESTEER) == -1) {
    perror(__FILE__ "resteer(): ");
  }
}

#ifdef UNIT_TEST

static int i = 0;

void recover(void) {
  printf("Recovering...\n");
  i++;
}

void loop(void) {
  for(; i < 10; ++i) {
    printf("%i\n", i);
    if(i == 5) {
      resteer(getpid());
      while(1);
    }
  }
}

int main(int argc, char **argv) {
  (void) argc;
  (void) argv;

  enableResteer(recover, loop);
  return 0;
}
#endif /* UNIT_TEST */
