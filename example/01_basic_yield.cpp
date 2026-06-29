// 01_basic_yield.cpp — cooperative round-robin yielding
// NOTE: Duff's Device requires all live-across-suspend state to be member
// fields.
#include "../libcss.hpp"
#include <cstdio>

struct Worker : css::Coroutine
{
  const char *label;
  int remaining;
  int tick;

  Worker (const char *l, int c) : label (l), remaining (c), tick (0) {}

  css::Status
  run (css::Scheduler & /*sched*/)
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
  css::Scheduler sched;
  sched.spawn<Worker> ("A", 3);
  sched.spawn<Worker> ("B", 3);
  sched.run ();
}
