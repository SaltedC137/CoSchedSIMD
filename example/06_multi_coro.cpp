// 06_multi_coro.cpp — mixed coroutine types in one scheduler
#include "../libcss.hpp"
#include <cstdio>

struct Yielder : css::Coroutine
{
  int remaining;
  Yielder (int count) : remaining (count) {}
  css::Status
  run (css::Scheduler & /*sched*/)
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

struct Sleeper : css::Coroutine
{
  int delay;
  Sleeper (int d) : delay (d) {}
  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (delay);
    std::printf ("[sleeper] awoke at tick %d\n", delay);
    CT_END ();
  }
};

struct Ping : css::Coroutine
{
  css::Channel &ch;
  int sent;
  int total;
  Ping (css::Channel &c, int n) : ch (c), sent (0), total (n) {}
  css::Status
  run (css::Scheduler &sched)
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

struct Pong : css::Coroutine
{
  css::Channel &ch;
  Pong (css::Channel &c) : ch (c) {}
  css::Status
  run (css::Scheduler &sched)
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
  css::Scheduler sched;
  css::Channel ch;
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
