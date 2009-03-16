#ifndef __IPC_H__
#define  __IPC_H__

#include <eza/arch/types.h>
#include <eza/mutex.h>
#include <eza/arch/atomic.h>
#include <ds/idx_allocator.h>
#include <eza/spinlock.h>
#include <eza/arch/arch_ipc.h>
#include <ipc/channel.h>
#include <ipc/port.h>

/* Blocking mode */
#define IPC_BLOCKED_ACCESS  0x1
#define IPC_AUTOREF         0x2

/**< Maximum numbers of vectors for I/O operations. */
#define MAX_IOVECS  8

#define UNTRUSTED_MANDATORY_FLAGS  (IPC_BLOCKED_ACCESS)

#define IPC_DEFAULT_PORT_MESSAGES  512
#define IPC_MAX_PORT_MESSAGES  512
#define IPC_DEFAULT_BUFFERS 512

/**< Number of pages statically allocated for a task for its large messages. */
#define IPC_PERTASK_PAGES 1

typedef struct __ipc_cached_data {
  void *cached_page1, *cached_page2;
  ipc_port_message_t cached_port_message;
} ipc_cached_data_t;

typedef struct __ipc_pstats {
  int foo[4];
} ipc_pstats_t;

typedef struct __task_ipc {
  mutex_t mutex;
  atomic_t use_count;  /* Number of tasks using this IPC structure. */

  /* port-related stuff. */
  ulong_t num_ports,max_port_num;
  ipc_gen_port_t **ports;
  idx_allocator_t ports_array;
  int allocated_ports;
  spinlock_t port_lock;

  /* Channels-related stuff. */
  idx_allocator_t channel_array;
  ulong_t num_channels,max_channel_num;
  ipc_channel_t **channels;
  spinlock_t channel_lock;
  int allocated_channels;
} task_ipc_t;

typedef struct __task_ipc_priv {
  /* Cached singletones for synchronous operations. */
  ipc_cached_data_t cached_data;
  ipc_pstats_t pstats;
} task_ipc_priv_t;

int setup_task_ipc(task_t *task);
void release_task_ipc_priv(task_ipc_priv_t *priv);

#define LOCK_IPC(ipc)  mutex_lock(&(ipc)->mutex)
#define UNLOCK_IPC(ipc) mutex_unlock(&(ipc)->mutex);

#define IPC_LOCK_PORTS(ipc) spinlock_lock(&ipc->port_lock)
#define IPC_UNLOCK_PORTS(ipc) spinlock_unlock(&ipc->port_lock)

#define IPC_LOCK_BUFFERS(ipc) spinlock_lock(&ipc->buffer_lock);
#define IPC_UNLOCK_BUFFERS(ipc) spinlock_unlock(&ipc->buffer_lock);

#define IPC_LOCK_CHANNELS(ipc) spinlock_lock(&ipc->channel_lock)
#define IPC_UNLOCK_CHANNELS(ipc) spinlock_unlock(&ipc->channel_lock)

#define REF_IPC_ITEM(c)  atomic_inc(&c->use_count)
#define UNREF_IPC_ITEM(c)  atomic_dec(&c->use_count)

void release_task_ipc(task_ipc_t *ipc);
void close_ipc_resources(task_ipc_t *ipc);
void dup_task_ipc_resources(task_ipc_t *ipc);
void *allocate_ipc_memory(long size);
void free_ipc_memory(void *addr,int size);

typedef struct __iovec {
  void *iov_base;
  size_t iov_len;
} iovec_t;

static inline task_ipc_t *get_task_ipc(task_t *t)
{
  task_ipc_t *ipc;

  LOCK_TASK_MEMBERS(t);
  if( t->ipc ) {
    atomic_inc(&t->ipc->use_count);
    ipc=t->ipc;
  } else {
    ipc=NULL;
  }
  UNLOCK_TASK_MEMBERS(t);

  return ipc;
}

long replicate_ipc(task_ipc_t *source,task_t *rcpt);
void initialize_ipc(void);

#endif
