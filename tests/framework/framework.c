#include <config.h>
#include <mstring/kernel.h>
#include <mstring/kprintf.h>
#include <mstring/smp.h>
#include <arch/scheduler.h>
#include <arch/types.h>
#include <mstring/task.h>
#include <test.h>
#include <mstring/swks.h>
#include <arch/interrupt.h>
#include <mstring/time.h>

#define LOOP_STEP 300

static bool __first_fault_hit=false;
static int num_failed=0;
static bool abort=false;

static bool __def_continue_testing(void)
{
  return (!abort && (!num_failed || (num_failed && !__first_fault_hit)) );
}

static void __check_for_spin(void)
{
  if( !__def_continue_testing() ) {
    interrupts_disable();
    for(;;);
  }
}

static void __def_passed(void)
{
  kprintf( "\n  PASSED\n\n" );
}

static void __def_failed(void)
{
  kprintf( "\n  FAILED\n\n" );
  num_failed++;


  __check_for_spin();
}

static void __def_set_first_fault_hit(bool v)
{
  __first_fault_hit=v;
}

static void __def_test_completion_loop(const char *id,bool *flag)
{
  uint64_t target_tick = system_ticks+LOOP_STEP;

  while( !*flag ) {
    if( system_ticks >= target_tick ) {
      kprintf("* [TEST COMPLETION LOOP] %d timer ticks elapsed.\n",
              LOOP_STEP);
      target_tick += LOOP_STEP;
    }
  }
}

static void __def_abort(void)
{
  abort=true;
  __check_for_spin();
}

static test_framework_t def_test_framework = {
  .passed=__def_passed,
  .failed=__def_failed,
  .printf=kprintf,
  .set_first_fault_hit=__def_set_first_fault_hit,
  .continue_testing=__def_continue_testing,
  .test_completion_loop=__def_test_completion_loop,
  .abort=__def_abort,
};

void run_tests(void)
{
  test_framework_t *tf=&def_test_framework;
  test_collection_t *tc;
  void *tc_ctx;

  kprintf( "Initializing test collection ..." );
  tc=create_test_collection(&tc_ctx);
  if( !tc->initialize(&tc_ctx) ) {
    kprintf( "\n** Can't initialize test collection !\n" );
    return;
  }

  add_testcases(tc,tc_ctx);

  kprintf( " %d testcase(s) found.\n",
           tc->get_num_testcases(tc_ctx));

#ifdef CONFIG_TEST_IGNORE_FAILS
  tf->set_first_fault_hit(false);
#else
  tf->set_first_fault_hit(true);
#endif

  tc->run(tf,tc_ctx);

  kprintf( "\n\nDone ! %d of %d testcases executed.\n",
           tc->get_num_executed(tc_ctx),
           tc->get_num_testcases(tc_ctx));
  kprintf( "Failed tests: %d\n\n",num_failed );
}
