// test_macros.cpp - CT_* macro integration: pc progress, zero-delay skip,
// condition waiting, channel handshake
// Macro constraint (consistent with the examples): case labels cannot jump
// past a local declaration with an initializer, so loop counters and value
// slots live in members or in external references.
#include "harness.hpp"

#include "../libccss.hpp"

struct Upper : ccss::Coroutine
{
  int *log;
  int *pos;
  Upper (int *l, int *p) : log (l), pos (p) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    (void)sched; // CT_YIELD does not touch sched (sleep does)
    CT_BEGIN ();
    log[(*pos)++] = 1;
    CT_YIELD ();
    log[(*pos)++] = 2;
    CT_SLEEP (1);
    log[(*pos)++] = 3;
    CT_END ();
  }
};

CCSS_TEST (macros_yield_pc_progress)
{
  ccss::Scheduler s;
  int log[4];
  int pos = 0;
  s.spawn<Upper> (log, &pos);

  s.step (); // pc 0: log[0]=1, yield
  CHECK_EQ (pos, 1);
  CHECK_EQ (log[0], 1);

  s.step (); // yield resumes: log[1]=2, parks to sleep
  CHECK_EQ (pos, 2);
  CHECK_EQ (log[1], 2);

  s.step (); // sleep(1) expires: log[2]=3, done
  CHECK_EQ (pos, 3);
  CHECK_EQ (log[2], 3);
  CHECK (!s.alive ());
}

struct ZeroSleep : ccss::Coroutine
{
  int *log;
  int *pos;
  ZeroSleep (int *l, int *p) : log (l), pos (p) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    (void)sched;
    CT_BEGIN ();
    log[(*pos)++] = 1;
    CT_SLEEP (0); // ticks == 0 -> skips sleeping, continues within this run
    log[(*pos)++] = 2;
    CT_END ();
  }
};

CCSS_TEST (macros_sleep_zero_skips)
{
  ccss::Scheduler s;
  int log[2];
  int pos = 0;
  s.spawn<ZeroSleep> (log, &pos);
  s.step ();
  CHECK_EQ (pos, 2); // both log entries happen within one run
  CHECK (!s.alive ());
}

struct FlagSetter : ccss::Coroutine
{
  bool *flag;
  int *set_tick;
  FlagSetter (bool *f, int *t) : flag (f), set_tick (t) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (3);
    *flag = true;
    *set_tick = sched.now ();
    CT_END ();
  }
};

struct FlagWaiter : ccss::Coroutine
{
  bool *flag;
  int *woke_tick;
  FlagWaiter (bool *f, int *w) : flag (f), woke_tick (w) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    (void)sched;
    CT_BEGIN ();
    CT_WAIT_UNTIL (*flag);
    *woke_tick = sched.now ();
    CT_END ();
  }
};

CCSS_TEST (macros_wait_until_flag)
{
  ccss::Scheduler s;
  bool flag = false;
  int set_tick = -1;
  int woke_tick = -1;
  s.spawn<FlagWaiter> (&flag, &woke_tick);
  s.spawn<FlagSetter> (&flag, &set_tick);
  s.run ();
  CHECK_EQ (set_tick, 3);  // setter runs in the step where tick == 3
  CHECK_EQ (woke_tick, 4); // the waiter observes it one step later (now = 4)
}

struct Feeder : ccss::Coroutine
{
  ccss::Channel &ch;
  int n;
  int idx; // values sent (macro constraint: counter lives in a member)
  Feeder (ccss::Channel &c, int n_, int start) : ch (c), n (n_), idx (start) {}
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
            CT_SLEEP (1); // queue full: retry next tick, never drop a value
          }
      }
    CT_END ();
  }
};

struct Gatherer : ccss::Coroutine
{
  ccss::Channel &ch;
  int n;
  int idx;
  int *sum;
  int recv_slot; // value slot: avoids an initializer crossing a case label
  Gatherer (ccss::Channel &c, int n_, int start, int *s)
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

CCSS_TEST (macros_recv_multi_value)
{
  ccss::Scheduler s;
  ccss::Channel ch;
  int sum = 0;
  s.spawn<Feeder> (ch, 5, 0);
  s.spawn<Gatherer> (ch, 5, 0, &sum);
  s.run ();
  CHECK_EQ (sum, 10); // 0+1+2+3+4
  CHECK (ch.empty ());
  CHECK (!s.alive ());
}

MAIN ()