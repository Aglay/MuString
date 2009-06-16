#ifndef __SIGINFO_H__
#define __SIGINFO_H__

#include <mstring/signal.h>

struct sigevent {
  int sigev_notify;
  int sigev_signo;
  union sigval sigev_value;
  tid_t tid;
};

#define INIT_SIGEVENT(e)                        \
  (e).sigev_notify=SIGEV_SIGNAL;               \
     (e).sigev_signo=SIGALRM                  \

#endif