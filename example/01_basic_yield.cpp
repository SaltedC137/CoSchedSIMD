// 01_basic_yield.cpp — cooperative round-robin yielding
// NOTE: Duff's Device requires all live-across-suspend state to be member
// fields.
#include "../libccss.hpp"
#include <cstdio>

struct Worker : ccss::Coroutine
{
  const char *label;
  int remaining;
  int tick;

  Worker (const char *l, int c) : label (l), remaining (c), tick (0) {}

  ccss::Status
  run (ccss::Scheduler & /*sched*/)
  {
    CT_BEGIN ();
    while (remaining > 0)
      {
        std::printf ("[%s] tick %d\n", label, tick);
        ++tick;
        --remaining;
        CT_YIELD ();
      }
    std::printf ("[%s] done\n", label);
    CT_END ();
  }
};

int
main ()
{
  ccss::Scheduler sched;
  sched.spawn<Worker> ("A", 3);
  sched.spawn<Worker> ("B", 3);
  sched.run ();
}
