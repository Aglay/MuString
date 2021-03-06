/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 * (c) Copyright 2009 Dan Kruchinin <dk@jarios.org>
 *
 */

#include <config.h>
#include <arch/interrupt.h>
#include <ds/list.h>
#include <sync/spinlock.h>
#include <mstring/interrupt.h>
#include <mstring/errno.h>
#include <mstring/string.h>
#include <mstring/kprintf.h>
#include <mstring/types.h>

#ifdef CONFIG_DEBUG_IRQ_ACTIVITY
#include <mstring/serial.h>
#endif

struct irq_controller *default_irqctrl = NULL;
static LIST_DEFINE(irqctls_list);
static SPINLOCK_DEFINE(irqctls_lock, "IRQ controllers");
static SPINLOCK_DEFINE(irq_lines_lock, "IRQ lines");
static struct irq_line irq_lines[NUM_IRQ_LINES];

#define IRQLINE_IS_ACTIVE(irqline)              \
  !!((irqline)->flags & IRQLINE_ACTIVE)

#define IRQ_CONTROLLERS_LOCK()   spinlock_lock(&irqctls_lock)
#define IRQ_CONTROLLERS_UNLOCK() spinlock_unlock(&irqctls_lock)

static void __unregister_irq_action(struct irq_line *iline,
                                    struct irq_action *iaction)
{
  list_del(&iaction->node);
  iaction->irq = IRQ_INVAL;
  iline->num_actions--;
}

static int __register_irq_line(struct irq_line *iline,
                               struct irq_controller *irqctrl)
{
  if (unlikely(IRQLINE_IS_ACTIVE(iline))) {
    return ERR(-EBUSY);
  }

  iline->irqctl = irqctrl;
  iline->flags |= IRQLINE_ACTIVE;
  list_init_head(&iline->actions);

  return 0;
}

static int __register_irq_action(struct irq_line *iline,
                                 struct irq_action *iaction,
                                 irq_t irq)
{
  if (unlikely(!IRQLINE_IS_ACTIVE(iline))) {
    return ERR(-EINVAL);
  }

  iaction->irq = irq;
  list_add2tail(&iline->actions, &iaction->node);
  iline->num_actions++;
  iline->irqctl->unmask_irq(irq);

  return 0;
}

int irq_mask(irq_t irq)
{
  struct irq_line *iline;
  uint_t irqstat;

  if (irq >= NUM_IRQ_LINES) {
    return ERR(-EINVAL);
  }

  iline = &irq_lines[irq];
  spinlock_lock_irqsave(&iline->irq_line_lock, irqstat);
  if (unlikely(!IRQLINE_IS_ACTIVE(iline))) {
    spinlock_unlock_irqrestore(&iline->irq_line_lock, irqstat);
    return ERR(-EBADF);
  }

  iline->irqctl->mask_irq(irq);
  spinlock_unlock_irqrestore(&iline->irq_line_lock, irqstat);

  return 0;
}

void irq_mask_all(void)
{
  struct irq_controller *irqctl;

  IRQ_CONTROLLERS_LOCK();
  list_for_each_entry(&irqctls_list, irqctl, ctl_node) {
    irqctl->mask_all();
  }

  IRQ_CONTROLLERS_UNLOCK();
}

int irq_unmask(irq_t irq)
{
  struct irq_line *iline;
  uint_t irqstat;

  if (irq >= NUM_IRQ_LINES) {
    return ERR(-EINVAL);
  }

  iline = &irq_lines[irq];
  spinlock_lock_irqsave(&iline->irq_line_lock, irqstat);
  if (unlikely(!IRQLINE_IS_ACTIVE(iline))) {
    spinlock_unlock_irqrestore(&iline->irq_line_lock, irqstat);
    return ERR(-EBADF);
  }

  iline->irqctl->unmask_irq(irq);
  spinlock_unlock_irqrestore(&iline->irq_line_lock, irqstat);

  return 0;
}

void irq_unmask_all(void)
{
  struct irq_controller *ictl;

  IRQ_CONTROLLERS_LOCK();
  list_for_each_entry(&irqctls_list, ictl, ctl_node) {
    ictl->unmask_all();
  }

  IRQ_CONTROLLERS_UNLOCK();
}

bool irq_line_is_registered(irq_t irq)
{
  struct irq_line *iline;
  uint_t irqstat;
  bool ret = false;

  ASSERT(irq < NUM_IRQ_LINES);
  iline = &irq_lines[irq];
  spinlock_lock_irqsave(&iline->irq_line_lock, irqstat);
  if (IRQLINE_IS_ACTIVE(iline)) {
    ret = true;
  }

  spinlock_unlock_irqrestore(&iline->irq_line_lock, irqstat);
  return ret;
}

int irq_register_action(irq_t irq, struct irq_action *action)
{
  struct irq_line *iline;
  uint_t irqstat;
  int ret;

  ASSERT(action != NULL);
  if (irq >= NUM_IRQ_LINES) {
    return ERR(-EINVAL);
  }

  iline = &irq_lines[irq];
  spinlock_lock_irqsave(&iline->irq_line_lock, irqstat);
  ret = __register_irq_action(iline, action, irq);
  if (!ret && (iline->num_actions == 1)) {
    iline->irqctl->unmask_irq(irq);
  }
  spinlock_unlock_irqrestore(&iline->irq_line_lock, irqstat);

  return ERR(ret);
}

int irq_unregister_action(struct irq_action *action)
{
  struct irq_action *irq_action;
  struct irq_line *iline;
  int ret = -ENOENT;
  uint_t irqstat;

  ASSERT(action != NULL);
  if (action->irq >= NUM_IRQ_LINES) {
    return ERR(-EINVAL);
  }

  iline = &irq_lines[action->irq];
  spinlock_lock_irqsave(&iline->irq_line_lock, irqstat);
  list_for_each_entry(&iline->actions, irq_action, node) {
    if (irq_action == action) {
      ret = 0;
      break;
    }
  }
  if (!ret) {
    __unregister_irq_action(iline, action);
  }
  if (!iline->num_actions) {
    iline->irqctl->mask_irq(action->irq);
  }

  spinlock_unlock_irqrestore(&iline->irq_line_lock, irqstat);
  return ERR(ret);
}

INITCODE void irq_register_controller(struct irq_controller *irqctl)
{
  ASSERT(irqctl != NULL);
  ASSERT(irqctl->mask_all != NULL);
  ASSERT(irqctl->unmask_all != NULL);
  ASSERT(irqctl->mask_irq != NULL);
  ASSERT(irqctl->unmask_irq != NULL);
  ASSERT(irqctl->ack_irq != NULL);
  ASSERT(irqctl->set_affinity != NULL);

  IRQ_CONTROLLERS_LOCK();
  list_add2tail(&irqctls_list, &irqctl->ctl_node);
  IRQ_CONTROLLERS_UNLOCK();
}

struct irq_controller *irq_get_controller(const char *ctrl_name)
{
  struct irq_controller *irqctrl;
  bool found = false;

  IRQ_CONTROLLERS_LOCK();
  list_for_each_entry(&irqctls_list, irqctrl, ctl_node) {
    if (!strcmp(irqctrl->name, ctrl_name)) {
      found = true;
      break;
    }
  }

  IRQ_CONTROLLERS_UNLOCK();
  if (!found) {
    return NULL;
  }

  return irqctrl;
}

int irq_line_register(irq_t irq, struct irq_controller *controller)
{
  struct irq_line *iline;
  uint_t irqstat;
  int ret;

  ASSERT(controller != NULL);
  if (irq >= NUM_IRQ_LINES) {
    return ERR(-EINVAL);
  }

  iline = &irq_lines[irq];
  spinlock_lock_irqsave(&iline->irq_line_lock, irqstat);
  ret = __register_irq_line(iline, controller);
  spinlock_unlock_irqrestore(&iline->irq_line_lock, irqstat);

  return ERR(ret);
}

struct irq_controller *irq_get_line_controller(irq_t irq)
{
  long irqstat;
  struct irq_controller *ctrl;
  struct irq_line *iline;

  if (irq >= NUM_IRQ_LINES) {
    return NULL;
  }

  iline = &irq_lines[irq];

  spinlock_lock_irqsave(&iline->irq_line_lock, irqstat);
  if (IRQLINE_IS_ACTIVE(iline)) {
    ctrl = iline->irqctl;
  } else {
    ctrl = NULL;
  }
  spinlock_unlock_irqrestore(&iline->irq_line_lock, irqstat);
  return ctrl;
}

int irq_register_free_line(struct irq_controller *irqctrl, irq_t *irq)
{
  irq_t i;
  uint_t irqstat;
  int ret = -EAGAIN;
  struct irq_line *iline;

  spinlock_lock_irqsave(&irq_lines_lock, irqstat);
  for (i = 0; i < NUM_IRQ_LINES; i++) {
    if (!irq_line_is_registered(i)) {
      iline = &irq_lines[i];
      ret = __register_irq_line(iline, irqctrl);
      break;
    }
  }
  spinlock_unlock_irqrestore(&irq_lines_lock, irqstat);

  if (!ret)
    *irq = i;

  return ERR(ret);
}

int irq_line_unregister(irq_t irq)
{
  struct irq_line *iline;
  struct irq_action *iaction;
  list_node_t *node, *safe_node;
  uint_t irqstat;

  if (irq >= NUM_IRQ_LINES) {
    return ERR(-EINVAL);
  }

  iline = &irq_lines[irq];
  spinlock_lock_irqsave(&iline->irq_line_lock, irqstat);
  if (unlikely(!IRQLINE_IS_ACTIVE(iline))) {
    spinlock_unlock_irqrestore(&iline->irq_line_lock, irqstat);
    return ERR(-EALREADY);
  }
  list_for_each_safe(&iline->actions, node, safe_node) {
    iaction = list_entry(node, struct irq_action, node);
    __unregister_irq_action(iline, iaction);
  }

  iline->flags &= ~IRQLINE_ACTIVE;
  spinlock_unlock_irqrestore(&iline->irq_line_lock, irqstat);

  return 0;
}

int irq_register_line_and_action(irq_t irq, struct irq_controller *irqctrl,
                                 struct irq_action *irqaction)
{
  int ret;
  uint_t irqstat;
  struct irq_line *iline;

  ASSERT(irqctrl != NULL);
  ASSERT(irqaction != NULL);
  if (irq >= NUM_IRQ_LINES) {
    return ERR(-EINVAL);
  }

  iline = &irq_lines[irq];
  spinlock_lock_irqsave(&iline->irq_line_lock, irqstat);
  ret = __register_irq_line(iline, irqctrl);
  if (ret) {
    goto out;
  }

  ret = __register_irq_action(iline, irqaction, irq);

out:
  spinlock_unlock_irqrestore(&iline->irq_line_lock, irqstat);
  return ERR(ret);
}

INITCODE void irqs_init(void)
{
  irq_t irq_num;
  struct irq_line *iline;

  for (irq_num = 0; irq_num < NUM_IRQ_LINES; irq_num++) {
    iline = &irq_lines[irq_num];
    memset(iline, 0, sizeof(*iline));
    list_init_head(&iline->actions);
    spinlock_initialize(&iline->irq_line_lock, "IRQ line");
  }

  arch_irqs_init();
  ASSERT(default_irqctrl != NULL);
}

void __do_handle_irq(irq_t irq)
{
  int handlers = 0;
  struct irq_line *iline;
  struct irq_action *action;
  uint_t irqstat;

  if (irq >= NUM_IRQ_LINES) {
    goto out;
  }

#ifdef CONFIG_DEBUG_IRQ_ACTIVITY
  serial_write_char('<');
  serial_write_char('0'+irq);
#endif

  iline = &irq_lines[irq];
  spinlock_lock_irqsave(&iline->irq_line_lock, irqstat);

  /* Call all handlers. */
  list_for_each_entry(&iline->actions, action, node) {
#ifdef CONFIG_DEBUG_IRQ_ACTIVITY
    serial_write_char('.');
#endif
    action->handler(action->priv_data);
    handlers++;
  }

  /* Ack this interrupt. */
  iline->irqctl->ack_irq(irq);
  iline->stat.num_irqs++;
  if (!handlers) {
    iline->stat.num_sp_irqs++;
  }

#ifdef CONFIG_DEBUG_IRQ_ACTIVITY
  if( handlers > 0) {
    serial_write_char('>');
  }
#endif

  spinlock_unlock_irqrestore(&iline->irq_line_lock, irqstat);
#ifdef CONFIG_DEBUG_IRQ_ACTIVITY
  serial_write_char('Z');
#endif

out:
  return;
}

