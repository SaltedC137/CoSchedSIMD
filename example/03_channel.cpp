// 03_channel.cpp — producer-consumer with Channel
#include "../libccss.hpp"
#include <cstdio>

struct Producer : ccss::Coroutine
{
  ccss::Channel &ch;
  int sent;

  Producer (ccss::Channel &c) : ch (c), sent (0) {}

  ccss::Status
  run (ccss::Scheduler &sched)
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

struct Consumer : ccss::Coroutine
{
  ccss::Channel &ch;
  int received;

  Consumer (ccss::Channel &c) : ch (c), received (0) {}

  ccss::Status
  run (ccss::Scheduler &sched)
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
  ccss::Scheduler sched;
  ccss::Channel ch;
  ch.reserve (3);
  sched.spawn<Producer> (ch);
  sched.spawn<Consumer> (ch);
  sched.run ();
}
