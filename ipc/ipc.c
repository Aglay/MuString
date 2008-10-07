#include <eza/arch/types.h>
#include <ipc/ipc.h>
#include <mm/pfalloc.h>
#include <mm/page.h>
#include <ds/linked_array.h>
#include <mlibc/string.h>

void initialize_ipc(void)
{
}

task_ipc_t *allocate_task_ipc(void)
{
  page_frame_t *p = alloc_page(AF_PGEN | AF_ZERO);
  if( p!= NULL ) {
    task_ipc_t *ipc = (task_ipc_t*)pframe_to_virt(p);

    memset(ipc,0,sizeof(task_ipc_t));

    atomic_set(&ipc->use_count,1);
    spinlock_initialize(&ipc->port_lock, "");
    semaphore_initialize(&ipc->sem,1);
    spinlock_initialize(&ipc->buffer_lock, "");

    ipc->cached_data.cached_page1 = alloc_pages_addr(1, AF_PGEN | AF_ZERO);
    ipc->cached_data.cached_page2 = alloc_pages_addr(1, AF_PGEN | AF_ZERO);

    return ipc;
  }
  return NULL;
  /* TODO: [mt] allocate task_ipc_t via slabs ! */
}
