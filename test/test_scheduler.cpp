// test_scheduler.cpp - scheduler semantics + regression for known defects
// Regression cases:
//   scheduler_sleep_current_clamps - review #2: oversized delays must not be
//                                    truncated by int32 cast in CT_SLEEP
//   scheduler_retire_ready_race   - review #1: retired coroutines must be
//                                    removed from `ready` (use-after-free)
#include "harness.hpp"

#include "../libccss.hpp"

struct Ordered : ccss::Coroutine
{
  int id;
  int limit;
  int *seq;
  int &pos;
  int i; // loop counter: case labels cannot jump past a local declaration
  Ordered (int id_, int lim, int *s, int &p)
      : id (id_), limit (lim), seq (s), pos (p), i (0)
  {
  }
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    (void)sched;
    CT_BEGIN ();
    for (; i < limit; ++i)
      {
        seq[pos++] = id;
        CT_YIELD ();
      }
    CT_END ();
  }
};

CCSS_TEST (scheduler_yield_round_robin)
{
  ccss::Scheduler s;
  int seq[6];
  int pos = 0;
  s.spawn<Ordered> (0, 3, seq, pos);
  s.spawn<Ordered> (1, 3, seq, pos);
  s.run ();
  const int expect[] = { 0, 1, 0, 1, 0, 1 };
  for (int i = 0; i < 6; ++i)
    {
      CHECK_EQ (seq[i], expect[i]);
    }
  CHECK (!s.alive ());
}

struct AwakeAt : ccss::Coroutine
{
  int delay;
  int &wake_tick;
  AwakeAt (int d, int &w) : delay (d), wake_tick (w) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (delay);
    wake_tick = sched.now ();
    CT_END ();
  }
};

CCSS_TEST (scheduler_sleep_alignment)
{
  ccss::Scheduler s;
  int w = -1;
  s.spawn<AwakeAt> (3, w);
  s.run ();
  CHECK_EQ (w, 3);        // sleep(n) wakes exactly at tick n
  CHECK_EQ (s.now (), 4); // one more step for the closing tick
}

struct LongSleep : ccss::Coroutine
{
  int &awakened;
  explicit LongSleep (int &a) : awakened (a) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (8589934592ULL); // 2^33: int32 truncation would yield 0
    ++awakened; // pre-fix #2 the coroutine reaches this on the first step
    CT_END ();
  }
};

CCSS_TEST (scheduler_sleep_current_clamps)
{
  ccss::Scheduler s;
  int awakened = 0;
  s.spawn<LongSleep> (awakened);
  for (int i = 0; i < 8; ++i)
    {
      s.step ();
    }
  CHECK_EQ (awakened, 0);
  CHECK_EQ (s.sleep_mgr.size (), 1u); // still parked; nowhere near expiry
  CHECK (s.alive ());
}

struct Victim : ccss::Coroutine
{
  int &runs;
  explicit Victim (int &r) : runs (r) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    ++runs;
    CT_BEGIN ();
    CT_SLEEP (3); // wakes at tick 3
    CT_YIELD ();  // finished, parked in ready - Hunter retires it this tick
    CT_END ();
  }
};

struct Hunter : ccss::Coroutine
{
  ccss::Coroutine *victim;
  bool &retired;
  Hunter (ccss::Coroutine *v, bool &r) : victim (v), retired (r) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (3); // wakes with Victim, runs after it
    if (!victim->done ())
      {
        sched.retire (victim);
        retired = true;
      }
    CT_END ();
  }
};

CCSS_TEST (scheduler_retire_ready_race)
{
  ccss::Scheduler s;
  int runs = 0;
  bool retired = false;
  Victim *v = s.spawn<Victim> (runs);
  s.spawn<Hunter> (v, retired);
  s.run ();
  // Post-fix #1: Victim is removed from ready when retired, so it runs
  // exactly twice. Pre-fix: the freed pointer stays in ready - caught by
  // ASan/UBSan, or (unsanitized) Victim runs again (runs > 2).
  CHECK (retired);
  CHECK_EQ (runs, 2);
  CHECK (!s.alive ());
}

struct CountingEnder : ccss::Coroutine
{
  int &runs;
  explicit CountingEnder (int &r) : runs (r) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    (void)sched;
    ++runs;
    CT_BEGIN ();
    CT_END ();
  }
};

CCSS_TEST (scheduler_retire_double_safe)
{
  ccss::Scheduler s;
  int runs = 0;
  CountingEnder *c = s.spawn<CountingEnder> (runs);
  s.retire (c);
  s.retire (c); // double retire -> no-op
  CHECK_EQ (s.alive_size (), 0u);
  s.run ();
  CHECK_EQ (runs, 0); // retired coroutines must never run
}

struct SleeperW : ccss::Coroutine
{
  int &awoke;
  explicit SleeperW (int &a) : awoke (a) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (1000);
    awoke = sched.now ();
    CT_END ();
  }
};

CCSS_TEST (scheduler_wake_sleeping_early)
{
  ccss::Scheduler s;
  int awoke = -1;
  SleeperW *c = s.spawn<SleeperW> (awoke);
  s.step ();
  CHECK_EQ (s.sleep_mgr.size (), 1u);
  s.wake (c);
  CHECK_EQ (s.sleep_mgr.size (), 0u);   // synchronously removed from sleep
  CHECK_EQ (c->status, ccss::CT_READY); // parked into ready
  s.step ();                            // resumes immediately
  CHECK_EQ (awoke, 1);
}

CCSS_TEST (scheduler_wake_ready_noop)
{
  ccss::Scheduler s;
  int awoke = -1;
  SleeperW *c = s.spawn<SleeperW> (awoke);
  s.wake (c); // already READY -> no-op, must not duplicate the queue entry
  CHECK_EQ (s.ready.size (), 1u);
  s.step (); // proceeds to sleep normally
  CHECK_EQ (s.sleep_mgr.size (), 1u);
  CHECK_EQ (awoke, -1);
}

struct RecvWaiter : ccss::Coroutine
{
  ccss::Channel &ch;
  int &got;
  RecvWaiter (ccss::Channel &c, int &g) : ch (c), got (g) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_RECV (ch, got);
    CT_END ();
  }
};

CCSS_TEST (scheduler_wake_waiting_clears_channel)
{
  ccss::Scheduler s;
  ccss::Channel ch;
  int g = -1;
  RecvWaiter *c = s.spawn<RecvWaiter> (ch, g);
  s.step ();
  CHECK_EQ (ch.waiters.size (), 1u);
  s.wake (c);
  CHECK (ch.waiters.empty ());
  CHECK_EQ (c->channel, nullptr);
  s.step (); // recv fails after resume -> re-parks
  CHECK_EQ (ch.waiters.size (), 1u);
}

static int g_live = 0;

struct Tracker : ccss::Coroutine
{
  Tracker () { ++g_live; }
  ~Tracker () { --g_live; }
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (3);
    CT_END ();
  }
};

CCSS_TEST (scheduler_destruction_releases_all)
{
  {
    ccss::Scheduler s;
    s.spawn<Tracker> ();
    s.spawn<Tracker> ();
    CHECK_EQ (g_live, 2);
    s.step (); // both asleep
  } // destructor reclaims every coroutine, sleeping ones included
  CHECK_EQ (g_live, 0);
}

CCSS_TEST (scheduler_run_empty)
{
  ccss::Scheduler s;
  s.run (); // empty scheduler returns immediately
  CHECK (!s.alive ());
}

MAIN ()