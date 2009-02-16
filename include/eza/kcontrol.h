#ifndef __KCONTROL_H__
#define __KCONTROL_H__

#include <eza/arch/types.h>

/* Top level node related to all kernel parameters. */
#define KCTRL_KERNEL             0
   /* Kernel parameters related to boot process. */
   #define KCTRL_BOOT_INFO          0
     /* Start physical page frame of the initial RAM disk loaded by the boot loader. */
     #define KCTRL_INITRD_START_PAGE  0
     /* Size of initial RAM disk in pages. */
     #define KCTRL_INITRD_SIZE        1

#define KCTRL_MAX_NAME_LEN  8

typedef struct __kcontrol_args {
  int *name;
  unsigned name_len;
  void *old_data;
  int *old_data_size;
  void *new_data;
  int new_data_size;
} kcontrol_args_t;

struct __kcontrol_node;
typedef long (*kcontrol_logic_t)(struct __kcontrol_node *node,
                                 struct __kcontrol_args *args);

typedef enum {
  KCTRL_DATA_LONG=0,
  KCTRL_DATA_CHAR=1,
} kcontrol_data_type_t;

typedef struct __kcontrol_node {
  int id;
  kcontrol_data_type_t type;
  unsigned int data_size,max_data_size;
  void *data;
  kcontrol_logic_t logic;
  struct __kcontrol_node *subdirs;
  unsigned int num_subdirs;
} kcontrol_node_t;

#endif
