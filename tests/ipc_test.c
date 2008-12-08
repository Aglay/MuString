#include <eza/kernel.h>
#include <mlibc/kprintf.h>
#include <eza/smp.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/scheduler.h>
#include <eza/swks.h>
#include <mlibc/string.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/preempt.h>
#include <eza/spinlock.h>
#include <ipc/ipc.h>
#include <ipc/port.h>
#include <eza/arch/asm.h>
#include <eza/arch/preempt.h>
#include <kernel/syscalls.h>
#include <eza/uinterrupt.h>
#include <ipc/poll.h>
#include <eza/gc.h>
#include <ipc/gen_port.h>
#include <ipc/channel.h>
#include <test.h>
#include <mm/slab.h>
#include <eza/errno.h>

#define TEST_ID  "IPC subsystem test"
#define SERVER_THREAD  "[SERVER THREAD] "
#define CLIENT_THREAD  "[CLIENT THREAD] "

#define DECLARE_TEST_CONTEXT  ipc_test_ctx_t *tctx=(ipc_test_ctx_t*)ctx; \
  test_framework_t *tf=tctx->tf

#define SERVER_NUM_PORTS  10
#define NON_BLOCKED_PORT_ID (SERVER_NUM_PORTS-1)
#define BIG_MESSAGE_PORT_ID 5

#define TEST_ROUNDS  3
#define SERVER_NUM_BLOCKED_PORTS  NON_BLOCKED_PORT_ID

typedef struct __ipc_test_ctx {
  test_framework_t *tf;
  ulong_t server_pid;
  bool tests_finished;
} ipc_test_ctx_t;

typedef struct __thread_port {
  ulong_t port_id,server_pid;
  ipc_test_ctx_t *tctx;
  bool finished_tests;
} thread_port_t;

#define MAX_TEST_MESSAGE_SIZE 512
static char __server_rcv_buf[MAX_TEST_MESSAGE_SIZE];
static char __client_rcv_buf[MAX_TEST_MESSAGE_SIZE];

#define BIG_MESSAGE_SIZE  (1500*1024)
static uint8_t __big_message_pattern[BIG_MESSAGE_SIZE+sizeof(int)];
static uint8_t __big_message_server_buf[BIG_MESSAGE_SIZE+sizeof(int)];
static uint8_t __big_message_client_buf[BIG_MESSAGE_SIZE+sizeof(int)];

static char *patterns[TEST_ROUNDS]= {
  "1",
  "1111111111111111111111111111112222222222222222222222222222222222222222222222222",
  "55555555555555555555555555555555555555555555555555555555555555555555555555555555"
  "55555555555555555555555555555555555555555555555555555555555555555555555555555555"
  "55555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555555",
};

static bool __verify_message(ulong_t id,char *msg)
{
  return !memcmp(patterns[id],msg,strlen(patterns[id])+1);
}

static bool __verify_big_message(uint8_t *buf,ulong_t *diff_offset)
{
  int i;
  uint8_t *p=__big_message_pattern;

  for(i=0;i<BIG_MESSAGE_SIZE;i++) {
    if( *p != *buf ) {
      *diff_offset=i;
      return false;
    }
    p++;
    buf++;
  }
  return true;
}

static void __client_thread(void *ctx)
{
  DECLARE_TEST_CONTEXT;
  int i;
  int channels[SERVER_NUM_PORTS];
  status_t r;

  tf->printf(CLIENT_THREAD "Opening %d channels.\n",SERVER_NUM_PORTS );
  for(i=0;i<SERVER_NUM_PORTS;i++) {
    channels[i]=sys_open_channel(tctx->server_pid,i);
    if( channels[i] != i ) {
      tf->printf(CLIENT_THREAD "Channel number mismatch: %d instead of %d\n",
                 channels[i],i);
      tf->failed();
      goto exit_test;
    }
  }
  tf->passed();

  tf->printf(CLIENT_THREAD "Trying to open a channel to insufficient port number.\n" );
  r=sys_open_channel(tctx->server_pid,SERVER_NUM_PORTS);
  if( r == -EINVAL ) {
    tf->passed();
  } else {
    tf->failed();
  }

  /****************************************************************
   * Sending messages in blocking mode.
   ****************************************************************/
  tf->printf(CLIENT_THREAD "Sending messages in blocking mode.\n" );  
  for(i=0;i<TEST_ROUNDS;i++) {
    r=sys_port_send(channels[i],IPC_BLOCKED_ACCESS,
                    (ulong_t)patterns[i],strlen(patterns[i])+1,
                    (ulong_t)__client_rcv_buf,sizeof(__client_rcv_buf));
    if( r < 0 ) {
      tf->printf(CLIENT_THREAD "Error while sending data over channel N%d : %d\n",
                 channels[i],r);
      tf->failed();
      goto exit_test;
    } else {
        if( r == strlen(patterns[i])+1 ) {
          if( !__verify_message(i,__client_rcv_buf) ) {
            tf->printf(CLIENT_THREAD "Reply to message N%d mismatch.\n",
                       i);
            tf->failed();
          }
        } else {
          tf->printf(CLIENT_THREAD "Reply to message N%d has insufficient length: %d instead of %d\n",
                     i,r,strlen(patterns[i])+1);
          tf->failed();
        }
    }
  }
  tf->printf(CLIENT_THREAD "All messages were successfully processed.\n" );
  tf->passed();

  /****************************************************************
   * Sending a big message in blocking mode.
   ****************************************************************/
  tf->printf(CLIENT_THREAD "Sending a big message in blocking mode.\n" );
  r=sys_port_send(channels[BIG_MESSAGE_PORT_ID],IPC_BLOCKED_ACCESS,
                  (ulong_t)__big_message_pattern,BIG_MESSAGE_SIZE,
                  (ulong_t)__big_message_client_buf,BIG_MESSAGE_SIZE);
  if( r < 0 ) {
    tf->printf(CLIENT_THREAD "Error while sending a big message over channel N%d : %d\n",
               channels[BIG_MESSAGE_PORT_ID],r);
    tf->failed();
    goto exit_test;
  } else {
    kprintf( CLIENT_THREAD "Got big reply: %d bytes.\n",r );
    if( r == BIG_MESSAGE_SIZE ) {
      ulong_t offt;

      if( !__verify_big_message(__big_message_client_buf,&offt) ) {
        tf->printf(CLIENT_THREAD "Server reply mismatch at offset: %d\n",
                   offt);
        tf->failed();
      } else {
        tf->printf(CLIENT_THREAD "Big message was successfully replied.\n" );
        tf->passed();
      }
    } else {
      tf->printf(CLIENT_THREAD "Server reply has insufficient length: %d instead of %d\n",
                 r,BIG_MESSAGE_SIZE);
      tf->failed();
    }
  }

  /****************************************************************
   * Sending messages in non-blocking mode.
   ****************************************************************/
  tf->printf(CLIENT_THREAD "Trying to send a non-blocking message to a blocking channel.\n" );
  r=sys_port_send(channels[0],0,
                  (ulong_t)patterns[0],strlen(patterns[0])+1,
                  (ulong_t)__client_rcv_buf,sizeof(__client_rcv_buf));
  if( r == -EINVAL ) {
    tf->passed();
  } else {
    tf->failed();
  }

  tf->printf(CLIENT_THREAD "Sending messages in non-blocking mode.\n" );
  for( i=0; i<TEST_ROUNDS;i++ ) {
    r=sys_port_send(channels[NON_BLOCKED_PORT_ID],0,
                    (ulong_t)patterns[i],strlen(patterns[i])+1,
                    0,0);
    if( r < 0 ) {
      tf->printf(CLIENT_THREAD "Error while sending data over channel N%d : %d\n",
                 channels[NON_BLOCKED_PORT_ID],r);
      tf->failed();
      goto exit_test;
    }
  }
  tf->printf(CLIENT_THREAD "All messages were successfully sent.\n" );
  tf->passed();

  goto exit_test;
exit_test:
  sys_exit(0);
}

#define POLL_CLIENT "[POLL CLIENT] "
static void __poll_client(void *d)
{
  thread_port_t *tp=(thread_port_t *)d;
  ipc_test_ctx_t *tctx=tp->tctx;
  test_framework_t *tf=tctx->tf;
  ulong_t channel,port;
  status_t i,r;
  ulong_t msg_id;
  char client_rcv_buf[MAX_TEST_MESSAGE_SIZE];

  port=tp->port_id;
  r=sys_open_channel(tp->server_pid,port);
  if( r < 0 ) {
    tf->printf(POLL_CLIENT "Can't open a channel to %d:%d ! r=%d\n",
               tp->server_pid,port,r);
    tf->abort();
  }
  channel=r;

  msg_id=port % TEST_ROUNDS;
  for(i=0;i<TEST_ROUNDS;i++) {
    tf->printf(POLL_CLIENT "Sending message to port %d.\n",port );
    r=sys_port_send(channel,IPC_BLOCKED_ACCESS,
                    (ulong_t)patterns[msg_id],strlen(patterns[msg_id])+1,
                    (ulong_t)client_rcv_buf,sizeof(client_rcv_buf));
    tf->printf("ok\n");
    if( r < 0 ) {
      tf->printf(POLL_CLIENT "Error occured while sending message: %d\n",r);
      tf->failed();
    } else {
      if( r == strlen(patterns[msg_id])+1 ) {
        if( !__verify_message(msg_id,client_rcv_buf) ) {
          tf->printf(POLL_CLIENT "Reply to my message mismatches !\n");
          tf->failed();
        }
      } else {
        tf->printf(POLL_CLIENT "Reply to my message has insufficient length: %d instead of %d\n",
                   r,strlen(patterns[msg_id])+1);
        tf->failed();
      }

      tf->printf(POLL_CLIENT "Message from port %d was successfully replied.\n",
                 port);
      sleep(HZ/50);
    }
  }

  /* Special case: test client wake up during port close. */
  if( port == 0 ) {
    tf->printf(POLL_CLIENT "Testing correct client wake-up on port closing...\n" );
    r=sys_port_send(channel,IPC_BLOCKED_ACCESS,
                    (ulong_t)patterns[msg_id],strlen(patterns[msg_id])+1,
                    (ulong_t)client_rcv_buf,sizeof(client_rcv_buf));
    if( r == -EPIPE ) {
      tf->printf( POLL_CLIENT "Got -EPIPE. It seems that kernel woke us properly.\n" );
      tp->finished_tests=true;
    } else {
      tf->printf(POLL_CLIENT "Insufficient return value upon wake-up on port closing: %d\n",
                 r);
      tf->failed();
    }
  } else {
    tp->finished_tests=true;
  }

  tf->printf(POLL_CLIENT "Exiting (target port: %d)\n",port );
  sys_exit(0);
}

static void __ipc_poll_test(ipc_test_ctx_t *tctx,int *ports)
{
  status_t i,r,j;
  pollfd_t fds[SERVER_NUM_BLOCKED_PORTS];
  thread_port_t poller_ports[SERVER_NUM_BLOCKED_PORTS];
  test_framework_t *tf=tctx->tf;
  ulong_t polled_clients;
  port_msg_info_t msg_info;
  char server_rcv_buf[MAX_TEST_MESSAGE_SIZE];
  
  for(i=0;i<SERVER_NUM_BLOCKED_PORTS;i++) {
    poller_ports[i].port_id=ports[i];
    poller_ports[i].tctx=tctx;
    poller_ports[i].server_pid=current_task()->pid;
    poller_ports[i].finished_tests=false;

    if( kernel_thread(__poll_client,&poller_ports[i],NULL ) ) {
      tf->printf( "Can't create a client thread for testing IPC poll !\n" );
      tf->abort();
    }
  }

  tf->printf(SERVER_THREAD "Client threads created. Ready for polling ports.\n");
  for(j=0;j<TEST_ROUNDS;j++) {
    ulong_t msg_id;

    polled_clients=SERVER_NUM_BLOCKED_PORTS;

    for(i=0;i<SERVER_NUM_BLOCKED_PORTS;i++) {
      fds[i].events=POLLIN | POLLRDNORM;
      fds[i].revents=0;
      fds[i].fd=ports[i];
    }

    while(polled_clients) {
      tf->printf( SERVER_THREAD "Polling ports (%d clients left) ...\n",polled_clients );
      r=sys_ipc_port_poll(fds,SERVER_NUM_BLOCKED_PORTS,NULL);
      if( r < 0 ) {
        tf->printf( SERVER_THREAD "Error occured while polling ports: %d\n",r );
        tf->failed();
      } else {
        tf->printf( SERVER_THREAD "Events occured: %d\n", r );
        polled_clients-=r;
      }
    }
    tf->printf( SERVER_THREAD "All clients sent their messages.\n" );

    /* Process all pending events. */
    for(i=0;i<SERVER_NUM_BLOCKED_PORTS;i++) {
      if( !fds[i].revents ) {
        tf->printf( SERVER_THREAD "Port N %d doesn't have any pending events \n",
                    fds[i].fd);
        tf->abort();
      }
      r=sys_port_receive(fds[i].fd,0,(ulong_t)server_rcv_buf,
                         sizeof(server_rcv_buf),&msg_info);
      if( r != 0 ) {
        tf->printf( SERVER_THREAD "Error during processing port N %d. r=%d\n",
                    fds[i].fd,r);
        tf->abort();
      } else {
        msg_id=fds[i].fd % TEST_ROUNDS;
        if( msg_info.msg_len == strlen(patterns[msg_id])+1 ) {
          if( !__verify_message(msg_id,server_rcv_buf) ) {
            tf->printf( SERVER_THREAD "Message N %d mismatch.\n",
                        fds[i].fd );
            tf->failed();
          }
        } else {
          tf->printf(SERVER_THREAD "Message N%d has insufficient length: %d instead of %d\n",
                     fds[i].fd,msg_info.msg_len,strlen(patterns[msg_id])+1);
          tf->failed();
        }
        /* Reply here. */
        r=sys_port_reply(fds[i].fd,msg_info.msg_id,
                         (ulong_t)patterns[msg_id],strlen(patterns[msg_id])+1);
        if( r ) {
          tf->printf(SERVER_THREAD "Error occured during replying via port %d. r=%d\n",
                     fds[i].fd,r);
          tf->abort();
        }
      }
    }
    tf->printf( SERVER_THREAD "All messages were replied. Now let's have some rest (HZ/2) ...\n" );
    sleep(HZ/2);
  }

  /*****************************************************************
   * Testing client wake-up on port closing.
   ****************************************************************/
  /* By now poll client that communicates with port N0 is still running
   * and waiting for us to reply it.
   * But we will just close the port to check if it is still sleeping,
   * or not.
   */
  tf->printf(SERVER_THREAD "Testing client wake-up during port closing.\n" );
  r=sys_port_receive(0,0,(ulong_t)server_rcv_buf,
                     sizeof(server_rcv_buf),&msg_info);
  if( r != 0 ) {
    tf->printf( SERVER_THREAD "Error during receiving message for client wake-up. r=%d\n",
                r);
    tf->abort();
  }

  /* Simulate an accident port shutdown. */
  r=sys_close_port(0);
  if( r ) {
    tf->printf( SERVER_THREAD "Can't close port N 0. r=%d\n",r );
    tf->abort();
  }

  /* Let sleep for a while to allow the client to let us know about its state. */
  sleep(HZ/2);
  if( poller_ports[0].finished_tests ) {
    tf->printf( SERVER_THREAD "Clien't was successfully woken up.\n" );
    tf->passed();
  } else {
    tf->printf( SERVER_THREAD "Client wasn't woken up !\n",r );
    tf->failed();
  }
  tf->printf( SERVER_THREAD "All poll tests finished.\n" );
}

static void __server_thread(void *ctx)
{
  DECLARE_TEST_CONTEXT;
  int ports[SERVER_NUM_PORTS];
  int i;
  ulong_t flags;
  status_t r;
  port_msg_info_t msg_info;

  for( i=0;i<SERVER_NUM_PORTS;i++) {
    if( i != NON_BLOCKED_PORT_ID ) {
      flags=IPC_BLOCKED_ACCESS;
    } else {
      flags=0;
    }
    ports[i]=sys_create_port(flags,0);
    if( ports[i] != i ) {
      tf->printf(SERVER_THREAD "IPC port number mismatch: %d instead of %d\n",
                 ports[i],i);
      tf->failed();
      goto exit_test;
    }
  }

  tctx->server_pid=current_task()->pid;
  tf->printf(SERVER_THREAD "%d ports created.\n", SERVER_NUM_PORTS );

  if( kernel_thread(__client_thread,ctx,NULL) ) {
    tf->printf(SERVER_THREAD "Can't launch client thread.\n",
               ports[i],i);
    goto abort_test;
  }

  /********************************************************
   * Testing message delivery in blocking mode.
   ********************************************************/
  tf->printf(SERVER_THREAD "Testing message delivery in blocking mode.\n" );
  for(i=0;i<TEST_ROUNDS;i++) {
    r=sys_port_receive(i,IPC_BLOCKED_ACCESS,(ulong_t)__server_rcv_buf,
                       sizeof(__server_rcv_buf),&msg_info);
    tf->printf(SERVER_THREAD "Got a message !\n");
    if( r ) {
      tf->printf(SERVER_THREAD "Insufficient return value during 'sys_port_receive': %d\n",
                 r);
      tf->failed();
      goto exit_test;
    } else {
      if( msg_info.msg_id != 0 ) {
        tf->printf(SERVER_THREAD "Insufficient message id: %d instead of 0\n",
                   msg_info.msg_id);
        tf->failed();
      } else {
        if( msg_info.msg_len == strlen(patterns[i])+1 ) {
          if( !__verify_message(i,__server_rcv_buf) ) {
            tf->printf( SERVER_THREAD "Message N %d mismatch.\n",
                        i );
            tf->failed();
          } else {
            tf->printf( SERVER_THREAD "Message N %d was successfuly transmitted.\n",
                        i );
            tf->passed();
          }
        } else {
          tf->printf(SERVER_THREAD "Message N%d has insufficient length: %d instead of %d\n",
                     i,msg_info.msg_len,strlen(patterns[i])+1);
          tf->failed();
        }
      }
    }
    r=sys_port_reply(i,0,(ulong_t)patterns[i],strlen(patterns[i])+1);
    if( r ) {
      tf->printf(SERVER_THREAD "Insufficient return value during 'sys_port_reply': %d\n",
                 r);
      tf->failed();
      goto exit_test;
    }
  }

  /****************************************************************
   * Testing delivery of big messages in blocking mode.
   ****************************************************************/
  tf->printf(SERVER_THREAD "Testing delivery of a big message (%d bytes).\n",
             BIG_MESSAGE_SIZE);
  r=sys_port_receive(BIG_MESSAGE_PORT_ID,IPC_BLOCKED_ACCESS,
                     (ulong_t)__big_message_server_buf,
                     BIG_MESSAGE_SIZE,&msg_info);
  if( r ) {
      tf->printf(SERVER_THREAD "Insufficient return value during 'sys_port_receive': %d\n",
                 r);
      tf->failed();
      goto exit_test;
  } else {
    kprintf( SERVER_THREAD "Big message received: %d bytes.\n",msg_info.msg_len );
    if( msg_info.msg_len == BIG_MESSAGE_SIZE ) {
      ulong_t offt;

      if( !__verify_big_message(__big_message_server_buf,&offt) ) {
        tf->printf( SERVER_THREAD "Big message mismatch at offset: %d\n",
                    offt);
        tf->failed();
      } else {
        tf->printf( SERVER_THREAD "Big message was successfully transmitted.\n" );
        tf->passed();
      }
    } else {
        tf->printf(SERVER_THREAD "Message N%d has insufficient length: %d instead of %d\n",
                   msg_info.msg_id,msg_info.msg_len,BIG_MESSAGE_SIZE);
        tf->failed();
    }
  }
  r=sys_port_reply(BIG_MESSAGE_PORT_ID,msg_info.msg_id,
                   (ulong_t)__big_message_pattern,BIG_MESSAGE_SIZE);
  if( r ) {
    tf->printf(SERVER_THREAD "Insufficient return value during 'sys_port_reply': %d\n",
               r);
    tf->failed();
    goto exit_test;
  }

  /*****************************************************************
   * Testing message delivery in non-blocking mode (small messages).
   ****************************************************************/
  sleep(HZ/2);
  tf->printf(SERVER_THREAD "Testing message delivery in non-blocking mode.\n" );

  for(i=0;i<TEST_ROUNDS;i++) {
    r=sys_port_receive(NON_BLOCKED_PORT_ID,IPC_BLOCKED_ACCESS,
                       (ulong_t)__server_rcv_buf,
                       sizeof(__server_rcv_buf),&msg_info);
    if( r ) {
      tf->printf(SERVER_THREAD "Insufficient return value during 'sys_port_receive': %d\n",
                 r);
      tf->failed();
      goto exit_test;
    } else {
      if( msg_info.msg_len == strlen(patterns[i])+1 ) {
        if( !__verify_message(i,__server_rcv_buf) ) {
          tf->printf( SERVER_THREAD "Message N %d mismatch.\n",i );
          tf->failed();
        } else {
          tf->printf( SERVER_THREAD "Message N %d successfully transmitted.\n",i );
          tf->passed();
        }
      } else {
        tf->printf(SERVER_THREAD "Message N%d has insufficient length: %d instead of %d\n",
                   i,msg_info.msg_len,strlen(patterns[i])+1);
        tf->failed();
      }
    }
  }

  /*****************************************************************
   * Testing port polling.
   ****************************************************************/
  tf->printf(SERVER_THREAD "Testing IPC port polling.\n" );
  __ipc_poll_test(tctx,ports);

  goto exit_test;
abort_test:
  tf->abort();
exit_test:
  tctx->tests_finished=true;
  sys_exit(0);
}

static void __initialize_big_pattern(void)
{
  int i;
  int *p=(int *)__big_message_pattern;

  for(i=0;i<BIG_MESSAGE_SIZE/sizeof(int);i++) {
    *p++=i;
  }
}

static bool __ipc_tests_initialize(void **ctx)
{
  ipc_test_ctx_t *tctx=memalloc(sizeof(*tctx));

  if( tctx ) {
    memset(tctx,0,sizeof(*tctx));
    tctx->tests_finished=false;
    tctx->server_pid=10000;
    *ctx=tctx;
    __initialize_big_pattern();
    return true;
  }
  return false;
}

void __ipc_tests_run(test_framework_t *f,void *ctx)
{
  ipc_test_ctx_t *tctx=(ipc_test_ctx_t*)ctx;
  
  tctx->tf=f;

  if( kernel_thread(__server_thread,tctx,NULL) ) {
    f->printf( "Can't create server thread !" );
    f->abort();
  } else {
    f->test_completion_loop(TEST_ID,&tctx->tests_finished);
  }
}

void __ipc_tests_deinitialize(void *ctx)
{
  memfree(ctx);
}

testcase_t ipc_testcase={
  .id=TEST_ID,
  .initialize=__ipc_tests_initialize,
  .deinitialize=__ipc_tests_deinitialize,
  .run=__ipc_tests_run,
};
