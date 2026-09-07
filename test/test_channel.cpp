// test_channel.cpp - Channel semantics: bounded/unbounded, FIFO, waiter
// lifecycle Note: coroutine objects are owned and destroyed by the Scheduler;
// any assertion that outlives the coroutine must go through external state to
// avoid dereferencing a pointer freed by retire+cleanup.
#include "harness.hpp"

#include "../libccss.hpp"

#include <vector>

struct RecvOnce : ccss::Coroutine
{
  ccss::Channel &ch;
  int &got;
  RecvOnce (ccss::Channel &c, int &g) : ch (c), got (g) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_RECV (ch, got);
    CT_END ();
  }
};

CCSS_TEST (channel_fifo_send_recv)
{
  ccss::Scheduler s;    // send only borrows the sched reference when no waiter
  ccss::Channel ch (0); // unbounded; the default 64 cap is covered elsewhere
  for (int i = 0; i < 1000; ++i)
    {
      CHECK (ch.send (s, i));
    }
  CHECK_EQ (ch.size (), 1000u);
  for (int i = 0; i < 1000; ++i)
    {
      int v = -1;
      CHECK (ch.recv (v));
      CHECK_EQ (v, i);
    }
  CHECK (ch.empty ());
}

CCSS_TEST (channel_bounded_rejects_when_full)
{
  ccss::Scheduler s;
  ccss::Channel ch (2);
  CHECK (ch.send (s, 0));
  CHECK (ch.send (s, 1));
  CHECK (!ch.send (s, 2)); // full -> rejected
  int v = -1;
  CHECK (ch.recv (v));
  CHECK_EQ (v, 0);
  CHECK (ch.send (s, 2)); // capacity freed -> accepts again
  CHECK (ch.recv (v));
  CHECK_EQ (v, 1);
  CHECK (ch.recv (v));
  CHECK_EQ (v, 2);
  CHECK (!ch.recv (v));
}

CCSS_TEST (channel_unbounded_max0)
{
  ccss::Scheduler s;
  ccss::Channel ch (0); // max_queue_size == 0 -> never rejects
  for (int i = 0; i < 1000; ++i)
    {
      CHECK (ch.send (s, i));
    }
  CHECK_EQ (ch.size (), 1000u);
}

CCSS_TEST (channel_recv_empty_false)
{
  ccss::Channel ch;
  int v = -7;
  CHECK (!ch.recv (v));
  CHECK_EQ (v, -7);
}

CCSS_TEST (channel_wait_wake_lifo)
{
  ccss::Scheduler s;
  ccss::Channel ch;
  int g1 = -1;
  int g2 = -1;
  s.spawn<RecvOnce> (ch, g1);
  s.spawn<RecvOnce> (ch, g2);

  s.step (); // both fail to recv -> park in waiters
  CHECK_EQ (ch.waiters.size (), 2u);

  CHECK (ch.send (s, 42)); // LIFO: wakes the last waiter, g2
  s.step ();
  CHECK_EQ (g2, 42);
  CHECK_EQ (g1, -1);
  CHECK_EQ (ch.waiters.size (), 1u);

  CHECK (ch.send (s, 43));
  s.step ();
  CHECK_EQ (g1, 43);
  CHECK (ch.waiters.empty ());
}

CCSS_TEST (channel_wait_guard)
{
  ccss::Scheduler s;
  ccss::Channel a;
  ccss::Channel b;
  int g = -1;
  RecvOnce *c = s.spawn<RecvOnce> (a, g);
  CHECK_EQ (c->channel, nullptr);
  s.step (); // c binds to a
  CHECK_EQ (c->channel, &a);

  // waiting again while bound must be refused
  CHECK_EQ (b.wait (s, c), ccss::CT_DEAD);
  CHECK_EQ (c->channel, &a); // original binding stays intact

  // c waits forever: no run(); the destructor reclaims it
}

CCSS_TEST (channel_wake_clears_binding)
{
  ccss::Scheduler s;
  ccss::Channel ch;
  int g = -1;
  RecvOnce *c = s.spawn<RecvOnce> (ch, g);
  s.step ();
  CHECK_EQ (c->channel, &ch);
  CHECK_EQ (ch.waiters.size (), 1u);

  s.wake (c); // manual wake: binding and waiters are cleared together
  CHECK_EQ (c->channel, nullptr);
  CHECK (ch.waiters.empty ());

  s.step (); // recv fails again after resume -> re-parks (binding rebuilt)
  CHECK_EQ (c->channel, &ch);
  CHECK_EQ (ch.waiters.size (), 1u);

  // c waits forever: no run(); the destructor reclaims it
}

MAIN ()