// 06_multi_coro.cpp — mixed coroutine types in one scheduler
#include "../libccss.hpp"
#include <cstdio>

struct Yielder : ccss::Coroutine
{
  int remaining;
  Yielder (int count) : remaining (count) {}
  ccss::Status
  run (ccss::Scheduler & /*sched*/)
  {
    CT_BEGIN ();
    while (remaining > 0)
      {
        CT_YIELD ();
        --remaining;
      }
    CT_END ();
  }
};

struct Sleeper : ccss::Coroutine
{
  int delay;
  Sleeper (int d) : delay (d) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (delay);
    std::printf ("[sleeper] awoke at tick %d\n", delay);
    CT_END ();
  }
};

struct Ping : ccss::Coroutine
{
  ccss::Channel &ch;
  int sent;
  int total;
  Ping (ccss::Channel &c, int n) : ch (c), sent (0), total (n) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    while (sent < total)
      {
        ++sent;
        ch.send (sched, sent);
        std::printf ("[ping] sent %d\n", sent);
        CT_SLEEP (1);
      }
    CT_END ();
  }
};

struct Pong : ccss::Coroutine
{
  ccss::Channel &ch;
  Pong (ccss::Channel &c) : ch (c) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    for (;;)
      {
        int val;
        CT_RECV (ch, val);
        std::printf ("[pong] got %d\n", val);
        if (val >= 3)
          {
            break;
          }
      }
    CT_END ();
  }
};

int
main ()
{
  ccss::Scheduler sched;
  ccss::Channel ch;
  ch.reserve (4);

  sched.spawn<Yielder> (5);
  sched.spawn<Sleeper> (7);
  sched.spawn<Ping> (ch, 3);
  sched.spawn<Pong> (ch);

  std::printf ("[main] tick 0 — starting\n");
  sched.run ();
  std::printf ("[main] scheduler exhausted at tick %lld\n",
               static_cast<long long> (sched.now ()));
}
