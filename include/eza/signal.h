#ifndef __SIGNAL_H__
#define  __SIGNAL_H__

#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <eza/arch/signal.h>
#include <mm/slab.h>
#include <eza/bits.h>
#include <ds/list.h>

#define NUM_POSIX_SIGNALS  32
#define NUM_RT_SIGNALS     32

#define SIGRTMIN  NUM_POSIX_SIGNALS
#define SIGRTMAX  (SIGRTMIN+NUM_RT_SIGNALS)
#define NR_SIGNALS  SIGRTMAX

#define valid_signal(n)  ((n)>=0 && (n) < SIGRTMAX )
#define rt_signal(n)  ((n)>=SIGRTMIN && (n) < SIGRTMAX)

typedef struct __siginfo {
  int       si_signo;    /* Signal number */
  int       si_errno;    /* An errno value */
  int       si_code;     /* Signal code */
  pid_t     si_pid;      /* Sending process ID */
  uid_t     si_uid;      /* Real user ID of sending process */
  int       si_status;   /* Exit value or signal */
//  clock_t   si_utime;    /* User time consumed */
//  clock_t   si_stime;    /* System time consumed */
//  sigval_t  si_value;    /* Signal value */
  int       si_int;      /* POSIX.1b signal */
  void     *si_ptr;      /* POSIX.1b signal */
  void     *si_addr;     /* Memory location which caused fault */
  int       si_band;     /* Band event */
  int      si_fd;       /* File descriptor */
} siginfo_t;

#define SI_USER    0 /* Sent by user */
#define SI_KERNEL  1 /* Sent by kernel */

typedef void (*sa_handler_t)(int);
typedef void (*sa_sigaction_t)(int,siginfo_t *,void *);

typedef struct __sigaction {
  sa_handler_t sa_handler;
  sa_sigaction_t sa_sigaction;
  sigset_t   sa_mask;
  int        sa_flags;
  void     (*sa_restorer)(void);
} sigaction_t;

typedef struct __kern_sigaction {
  sa_sigaction_t sa_handler;
  sigset_t sa_mask;
  int sa_flags;
} kern_sigaction_t;

typedef struct __sighandlers {
  atomic_t use_count;
  kern_sigaction_t actions[NR_SIGNALS];
  spinlock_t lock;
} sighandlers_t;

#define SIGNAL_PENDING(set,sig)  arch_bit_test((set),sig)

sighandlers_t * allocate_signal_handlers(void);
static inline void put_signal_handlers(sighandlers_t *s)
{
  if( atomic_dec_and_test(&s->use_count ) ) {
    memfree(s);
  }
}

#define SIG_IGN  ((sa_sigaction_t)0)
#define SIG_DFL  ((sa_sigaction_t)1)

#define SIGHUP     1
#define SIGINT     2
#define SIGQUIT    3
#define SIGILL     4
#define SIGTRAP    5
#define SIGABRT    6
#define SIGIOT     6
#define SIGBUS     7
#define SIGFPE     8
#define SIGKILL    9
#define SIGUSR1    10
#define SIGSEGV    11
#define SIGUSR2    12
#define SIGPIPE	   13
#define SIGALRM	   14
#define SIGTERM	   15
#define SIGSTKFLT  16
#define SIGCHLD    17
#define SIGCONT    18
#define SIGSTOP    19
#define SIGTSTP    20
#define SIGTTIN	   21
#define SIGTTOU	   22
#define SIGURG	   23
#define SIGXCPU    24
#define SIGXFSZ	   25
#define SIGVTALRM  26
#define SIGPROF	   27
#define SIGWINCH   28
#define SIGIO      29
#define SIGPOLL    SIGIO
#define SIGPWR     30
#define SIGSYS     31

#define process_wide_signal(s)  ((s) & (_BM(SIGTERM) | _BM(SIGSTOP)) )

#define DEFAULT_IGNORED_SIGNALS (_BM(SIGCHLD) | _BM(SIGURG) | _BM(SIGWINCH))

typedef struct __sigq_item {
  list_node_t l;
  siginfo_t info;
} sigq_item_t;

void initialize_signals(void);

#define pending_signals_present(t) ((t)->siginfo.pending != 0)

#endif