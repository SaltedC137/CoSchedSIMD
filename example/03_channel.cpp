// 03_channel.cpp — producer-consumer with Channel
#include "../libcss.hpp"
#include <cstdio>

struct Producer : css::Coroutine
{
  css::Channel &ch;
  int sent;

  Producer (css::Channel &c) : ch (c), sent (0) {}

  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    while (sent < 5)
      {
        ++sent;
        std::printf ("[producer] send %d\n", sent);
        ch.send (sched, sent);
        CT_SLEEP (1);
      }
    std::printf ("[producer] done\n");
    CT_END ();
  }
};

struct Consumer : css::Coroutine
{
  css::Channel &ch;
  int received;

  Consumer (css::Channel &c) : ch (c), received (0) {}

  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    while (received < 5)
      {
        int val;
        CT_RECV (ch, val);
        std::printf ("[consumer] received %d\n", val);
        ++received;
      }
    std::printf ("[consumer] done\n");
    CT_END ();
  }
};

int
main ()
{
  css::Scheduler sched;
  css::Channel ch;
  ch.reserve (3);
  sched.spawn<Producer> (ch);
  sched.spawn<Consumer> (ch);
  sched.run ();
}
