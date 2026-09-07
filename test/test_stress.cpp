// test_stress.cpp - stress and lifecycle: bulk spawn, ping-pong, bounded
// queues, mass retire
#include "harness.hpp"

#include "../libccss.hpp"

#include <vector>

struct SleepOnce : ccss::Coroutine
{
  int delay;
  int &finished;
  SleepOnce (int d, int &f) : delay (d), finished (f) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (delay);
    ++finished;
    CT_END ();
  }
};

CCSS_TEST (stress_many_sleepers)
{
  constexpr int N = 1000;
  ccss::Scheduler s;
  int finished = 0;
  for (int i = 0; i < N; ++i)
    {
      s.spawn<SleepOnce> (i % 50 + 1, finished);
    }
  s.run ();
  CHECK_EQ (finished, N);
  CHECK (!s.alive ());
}

struct Pinger : ccss::Coroutine
{
  ccss::Channel &tx;
  ccss::Channel &rx;
  int rounds;
  bool &all_ok;
  int idx;
  int recv_slot;
  Pinger (ccss::Channel &t, ccss::Channel &r, int n, bool &ok, int start)
      : tx (t), rx (r), rounds (n), all_ok (ok), idx (start)
  {
  }
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    for (; idx < rounds; ++idx)
      {
        tx.send (sched, idx);
        CT_RECV (rx, recv_slot);
        if (recv_slot != idx + 1)
          {
            all_ok = false;
          }
      }
    CT_END ();
  }
};

struct Ponger : ccss::Coroutine
{
  ccss::Channel &rx;
  ccss::Channel &tx;
  int rounds;
  int idx;
  int recv_slot;
  Ponger (ccss::Channel &r, ccss::Channel &t, int n, int start)
      : rx (r), tx (t), rounds (n), idx (start)
  {
  }
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    for (; idx < rounds; ++idx)
      {
        CT_RECV (rx, recv_slot);
        tx.send (sched, recv_slot + 1);
      }
    CT_END ();
  }
};

CCSS_TEST (stress_pingpong)
{
  constexpr int ROUNDS = 500;
  ccss::Scheduler s;
  ccss::Channel p2g;
  ccss::Channel g2p;
  bool all_ok = true;
  s.spawn<Pinger> (p2g, g2p, ROUNDS, all_ok, 0); // P: tx->p2g, rx<-g2p
  s.spawn<Ponger> (p2g, g2p, ROUNDS, 0);         // G: rx<-p2g, tx->g2p
  s.run ();
  CHECK (all_ok);
  CHECK (p2g.empty ());
  CHECK (g2p.empty ());
  CHECK (!s.alive ());
}

struct Prod : ccss::Coroutine
{
  ccss::Channel &ch;
  int n;
  int idx;
  Prod (ccss::Channel &c, int n_, int start) : ch (c), n (n_), idx (start) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    for (; idx < n;)
      {
        if (ch.send (sched, idx))
          {
            ++idx;
          }
        else
          {
            CT_SLEEP (1); // queue full retry: no value may be dropped
          }
      }
    CT_END ();
  }
};

struct Cons : ccss::Coroutine
{
  ccss::Channel &ch;
  int n;
  int idx;
  int *sum;
  int recv_slot;
  Cons (ccss::Channel &c, int n_, int start, int *s)
      : ch (c), n (n_), idx (start), sum (s)
  {
  }
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    for (; idx < n; ++idx)
      {
        CT_RECV (ch, recv_slot);
        *sum += recv_slot;
      }
    CT_END ();
  }
};

CCSS_TEST (stress_producer_consumer_bounded)
{
  constexpr int N = 50;
  ccss::Scheduler s;
  ccss::Channel
      ch; // bounded (default 64) cannot hold everything -> retry path
  ch.reserve (2);
  int sum = 0;
  s.spawn<Prod> (ch, N, 0);
  s.spawn<Cons> (ch, N, 0, &sum);
  s.run ();
  CHECK_EQ (sum, N * (N - 1) / 2); // sum of 0..49, nothing lost
  CHECK (ch.empty ());
  CHECK (!s.alive ());
}

struct EndNow : ccss::Coroutine
{
  int &runs;
  explicit EndNow (int &r) : runs (r) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    (void)sched;
    ++runs;
    CT_BEGIN ();
    CT_END ();
  }
};

CCSS_TEST (stress_spawn_retire_half)
{
  constexpr int N = 200;
  ccss::Scheduler s;
  std::vector<int> runs (N, 0);
  std::vector<EndNow *> held;
  held.reserve (N);
  for (int i = 0; i < N; ++i)
    {
      held.push_back (s.spawn<EndNow> (runs[i]));
    }
  for (int i = 0; i < N / 2; ++i)
    {
      s.retire (held[i]); // mass-retire the first half (still in ready)
    }
  s.run ();
  for (int i = 0; i < N; ++i)
    {
      CHECK_EQ (runs[i], i < N / 2 ? 0 : 1); // retired must never run
    }
  CHECK (!s.alive ());
}

struct SleepMark : ccss::Coroutine
{
  int delay;
  int &wake_tick;
  SleepMark (int d, int &w) : delay (d), wake_tick (w) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (delay);
    wake_tick = sched.now ();
    CT_END ();
  }
};

CCSS_TEST (stress_exhaustion_tick_count)
{
  ccss::Scheduler s;
  int w1 = -1;
  int w2 = -1;
  s.spawn<SleepMark> (7, w1);
  s.spawn<SleepMark> (5, w2);
  s.run ();
  CHECK_EQ (w1, 7);
  CHECK_EQ (w2, 5);
  CHECK_EQ (s.now (), 8); // +1 for the step after the last wakeup
}

MAIN ()