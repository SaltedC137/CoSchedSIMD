// test_sleepmanager.cpp - SleepManager semantics
// This file is built twice (AVX2 and non-AVX2, see test/CMakeLists.txt)
// so the SIMD and scalar tick() paths are verified to behave identically.
#include "harness.hpp"

#include "../libccss.hpp"

#include <limits>
#include <vector>

struct Dummy : ccss::Coroutine
{
  int tag;
  Dummy (int t) : tag (t) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    (void)sched; // Dummy has no scheduling behavior; placeholder only
    return ccss::CT_DEAD;
  }
};

static std::vector<ccss::Coroutine *>
tick_once (ccss::SleepManager &m)
{
  std::vector<ccss::Coroutine *> ready;
  m.tick (ready);
  return ready;
}

CCSS_TEST (sleepmgr_tick_expiry_order)
{
  ccss::SleepManager m;
  Dummy *c0 = new Dummy (0);
  Dummy *c1 = new Dummy (1);
  Dummy *c2 = new Dummy (2);
  m.add (c0, 3);
  m.add (c1, 1);
  m.add (c2, 2);

  // tick1: 3,1,2 -> 2,0,1 -> only c1 expires
  std::vector<ccss::Coroutine *> r = tick_once (m);
  CHECK_EQ (r.size (), 1u);
  CHECK (r[0] == c1);
  CHECK_EQ (m.size (), 2u);

  // tick2: c2 expires; c0 remains
  r = tick_once (m);
  CHECK_EQ (r.size (), 1u);
  CHECK (r[0] == c2);
  CHECK_EQ (m.delays[0], 1);

  // tick3: c0 expires
  r = tick_once (m);
  CHECK_EQ (r.size (), 1u);
  CHECK (r[0] == c0);
  CHECK (m.empty ());

  // ticking an empty table is a no-op
  r = tick_once (m);
  CHECK (r.empty ());

  delete c0;
  delete c1;
  delete c2;
}

CCSS_TEST (sleepmgr_remove_all_positions)
{
  ccss::SleepManager m;
  Dummy *head = new Dummy (0);
  Dummy *mid = new Dummy (1);
  Dummy *tail = new Dummy (2);
  m.add (head, 5);
  m.add (mid, 5);
  m.add (tail, 5);

  m.remove (mid); // remove from the middle
  CHECK_EQ (m.size (), 2u);
  m.remove (tail); // remove from the tail
  CHECK_EQ (m.size (), 1u);
  m.remove (head); // remove from the head
  CHECK (m.empty ());

  m.remove (head); // removing a missing entry is a no-op
  CHECK (m.empty ());

  delete head;
  delete mid;
  delete tail;
}

CCSS_TEST (sleepmgr_immediate_expiry)
{
  ccss::SleepManager m;
  Dummy *a = new Dummy (0);
  Dummy *b = new Dummy (1);
  m.add (a, 0);
  m.add (b, -5); // negative values also expire on the first tick
  std::vector<ccss::Coroutine *> r = tick_once (m);
  CHECK_EQ (r.size (), 2u);
  CHECK (r[0] == a);
  CHECK (r[1] == b);
  CHECK (m.empty ());
  delete a;
  delete b;
}

CCSS_TEST (sleepmgr_empty_tick)
{
  ccss::SleepManager m;
  CHECK (tick_once (m).empty ());
}

CCSS_TEST (sleepmgr_many_ordered)
{
  constexpr int N = 100;
  ccss::SleepManager m;
  std::vector<Dummy *> cs;
  cs.reserve (N);
  for (int i = 0; i < N; ++i)
    {
      Dummy *d = new Dummy (i);
      cs.push_back (d);
      m.add (d, i + 1); // delay == i+1 -> expires on tick i+1
    }

  for (int t = 0; t < N; ++t)
    {
      std::vector<ccss::Coroutine *> r = tick_once (m);
      CHECK_EQ (r.size (), 1u);
      CHECK (r[0] == cs[t]);
      CHECK_EQ (m.size (), static_cast<std::size_t> (N - 1 - t));
      // compaction keeps order: the first survivor is the next expirer
      if (t + 1 < N)
        {
          CHECK_EQ (m.delays[0], 1);
        }
    }
  CHECK (m.empty ());

  for (Dummy *d : cs)
    {
      delete d;
    }
}

CCSS_TEST (sleepmgr_int32max_boundary)
{
  ccss::SleepManager m;
  Dummy *d = new Dummy (0);
  m.add (d, std::numeric_limits<int32_t>::max ());
  for (int i = 0; i < 3; ++i)
    {
      CHECK_EQ (m.size (), 1u); // nowhere near expiry; must not be dropped
      CHECK (tick_once (m).empty ());
    }
  delete d;
}

MAIN ()