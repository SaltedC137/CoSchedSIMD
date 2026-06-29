// 02_sleep.cpp — timed suspension
#include "../libcss.hpp"
#include <cstdio>

struct Timer : css::Coroutine
{
  const char *label;
  int delay;

  Timer (const char *l, int d) : label (l), delay (d) {}

  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    std::printf ("[%s] sleeping %d ticks…\n", label, delay);
    CT_SLEEP (delay);
    std::printf ("[%s] awake at tick %lld\n", label,
                 static_cast<long long> (sched.now ()));
    CT_END ();
  }
};

int
main ()
{
  css::Scheduler sched;
  sched.spawn<Timer> ("fast", 2); // wakes at tick 2
  sched.spawn<Timer> ("slow", 5); // wakes at tick 5
  sched.run ();
}
