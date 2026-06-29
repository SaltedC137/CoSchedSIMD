// 05_blinker.cpp — classic LED blinker
#include "../libccss.hpp"
#include <cstdio>

struct Blinker : ccss::Coroutine
{
  const char *name;
  int cycles;

  Blinker (const char *n) : name (n), cycles (0) {}

  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    while (cycles < 5)
      {
        std::printf ("[%s] ● ON\n", name);
        CT_SLEEP (3);
        std::printf ("[%s] ○ OFF\n", name);
        CT_SLEEP (2);
        ++cycles;
      }
    std::printf ("[%s] shutdown\n", name);
    CT_END ();
  }
};

int
main ()
{
  ccss::Scheduler sched;
  sched.spawn<Blinker> ("LED1");
  sched.spawn<Blinker> ("LED2");
  sched.run ();
}
