// 09_stress.cpp — limit tests for the cooperative coroutine scheduler
//
// Build (standalone):
//   g++ -std=c++17 -mavx2 -O2 -I.. -o stress 09_stress.cpp
//
// Known library issue uncovered by this test:
//   SleepManager::tick line ~181  CTICK_PREFETCH_R(&delays[i + 16])
//   When i + 16 >= delays.size() this dereferences operator[] past end,
//   which hardened std::vector (Fedora _GLIBCXX_ASSERTIONS) catches.
//   Workaround: compile with -D_GLIBCXX_ASSERTIONS=0 (CMake handles this).
//   Real fix: guard the prefetch with a bounds check in libcss.hpp.

#include "../libcss.hpp"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// ── helpers ──────────────────────────────────────────────────────────

static int g_checks = 0;
static int g_fails = 0;
static const int MAX_DELAY = 10000;

#define CHECK(cond)                                                           \
  do                                                                          \
    {                                                                         \
      ++g_checks;                                                             \
      if (!(cond))                                                            \
        {                                                                     \
          ++g_fails;                                                          \
          std::fprintf (stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__,      \
                        #cond);                                               \
        }                                                                     \
    }                                                                         \
  while (0)

// ── test 1: massive spawn + yield ────────────────────────────────────

struct Yielder : css::Coroutine
{
  int count = 0;
  Yielder (int n) : count (n) {}
  css::Status
  run (css::Scheduler & /*sched*/)
  {
    CT_BEGIN ();
    while (count > 0)
      {
        --count;
        CT_YIELD ();
      }
    CT_END ();
  }
};

static void
test_mass_spawn ()
{
  std::printf ("=== test 1: mass spawn (10 000 yielders) ===\n");
  css::Scheduler sched;
  for (int i = 0; i < 10000; ++i)
    sched.spawn<Yielder> (0); // single yield, immediate exit

  auto t0 = std::chrono::steady_clock::now ();
  sched.run ();
  auto t1 = std::chrono::steady_clock::now ();

  auto us = std::chrono::duration_cast<std::chrono::microseconds> (t1 - t0)
                .count ();
  CHECK (sched.alive_size () == 0);
  std::printf ("  10k coroutines in %lld µs (%.1f ns each)\n",
               static_cast<long long> (us), us * 1000.0 / 10000.0);
}

// ── test 2: precision sleep ─────────────────────────────────────────

static int verified_delays[128];

struct PrecisionSleeper : css::Coroutine
{
  int32_t delay;
  int64_t spawn_tick;
  PrecisionSleeper (int32_t d, int64_t st) : delay (d), spawn_tick (st) {}

  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (delay);
    int64_t elapsed = sched.now () - spawn_tick;
    CHECK (elapsed == delay);
    ++verified_delays[delay < 128 ? delay : 127];
    CT_END ();
  }
};

static void
test_precision_sleep ()
{
  std::printf ("=== test 2: precision sleep ===\n");
  std::memset (verified_delays, 0, sizeof (verified_delays));

  css::Scheduler sched;
  // Delays that cross SIMD boundaries: 1, 2, 7, 8, 9, 15, 16, 17, 31, 32
  int test_delays[] = { 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65 };
  for (int d : test_delays)
    {
      for (int i = 0; i < 20; ++i)
        sched.spawn<PrecisionSleeper> (d, 0);
    }

  sched.run ();

  for (int d : test_delays)
    {
      CHECK (verified_delays[d] == 20);
    }
  std::printf ("  all sleeps verified at exact tick\n");
}

// ── test 3: SIMD lane boundary stress ───────────────────────────────

struct LaneSleeper : css::Coroutine
{
  int32_t delay;
  LaneSleeper (int32_t d) : delay (d) {}

  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (delay);
    // All coroutines spawned at tick 0 — verify wake tick equals delay
    CHECK (sched.now () == delay);
    CT_END ();
  }
};

static void
test_simd_lanes ()
{
  std::printf ("=== test 3: SIMD lane boundaries ===\n");
  // SleepManager operates on 8-wide SIMD.  Trigger every lane-combination
  // path. We create groups that wake simultaneously (mask=255), staggered
  // (mixed mask), and none (mask=0) repeatedly.
  css::Scheduler sched;

  // Patterns: spawn co-routines at different delays so the SleepManager
  // runs phase-1 decrement + phase-2 compact on 8/16/24/32-element groups.
  for (int grp = 0; grp < 50; ++grp)
    {
      for (int i = 0; i < 16; ++i)
        {
          int d = (i < 8) ? 1 : 2; // half wake at tick+1, half at tick+2
          sched.spawn<LaneSleeper> (d);
        }
    }

  sched.run ();
  CHECK (sched.alive_size () == 0);
  std::printf ("  800 lane-stress coroutines passed\n");
}

// ── test 4: channel flood ───────────────────────────────────────────

struct ChanFiller : css::Coroutine
{
  css::Channel &ch;
  int n;
  int sent = 0;
  ChanFiller (css::Channel &c, int count) : ch (c), n (count) {}

  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    while (sent < n)
      {
        ch.send (sched, sent);
        ++sent;
        CT_SLEEP (0); // no delay, just yield-to-scheduler
      }
    CT_END ();
  }
};

struct ChanDrainer : css::Coroutine
{
  css::Channel &ch;
  int n;
  int got = 0;
  int prev = -1;
  ChanDrainer (css::Channel &c, int count) : ch (c), n (count) {}

  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    while (got < n)
      {
        int v;
        CT_RECV (ch, v);
        CHECK (v == prev + 1); // must be monotonic
        prev = v;
        ++got;
      }
    CT_END ();
  }
};

static void
test_channel_flood ()
{
  std::printf ("=== test 4: channel flood (100k messages) ===\n");
  css::Scheduler sched;
  css::Channel ch;
  ch.reserve (256);

  sched.spawn<ChanFiller> (ch, 100000);
  sched.spawn<ChanDrainer> (ch, 100000);

  auto t0 = std::chrono::steady_clock::now ();
  sched.run ();
  auto t1 = std::chrono::steady_clock::now ();

  auto us = std::chrono::duration_cast<std::chrono::microseconds> (t1 - t0)
                .count ();
  std::printf ("  100k msgs in %lld µs (%.1f ns/msg)\n",
               static_cast<long long> (us), us * 1000.0 / 100000.0);
}

// ── test 5: mixed workload (yield + sleep + channel + wait_until) ───

struct Flag
{
  int ready = 0;
};

struct MixYielder : css::Coroutine
{
  int n;
  MixYielder (int count) : n (count) {}
  css::Status
  run (css::Scheduler & /*sched*/)
  {
    CT_BEGIN ();
    while (n > 0)
      {
        --n;
        CT_YIELD ();
      }
    CT_END ();
  }
};

struct MixSleeper : css::Coroutine
{
  int32_t delay;
  bool *done;
  MixSleeper (int32_t d, bool *flag) : delay (d), done (flag) {}

  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (delay);
    *done = true;
    CT_END ();
  }
};

struct MixWaiter : css::Coroutine
{
  Flag &flag;
  bool *awake;
  MixWaiter (Flag &f, bool *a) : flag (f), awake (a) {}

  css::Status
  run (css::Scheduler & /*sched*/)
  {
    CT_BEGIN ();
    CT_WAIT_UNTIL (flag.ready > 0);
    *awake = true;
    CT_END ();
  }
};

struct MixSender : css::Coroutine
{
  css::Channel &ch;
  int *received;
  MixSender (css::Channel &c, int *r) : ch (c), received (r) {}

  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    ch.send (sched, 42);
    *received = 42;
    CT_END ();
  }
};

struct MixReceiver : css::Coroutine
{
  css::Channel &ch;
  int *val;
  MixReceiver (css::Channel &c, int *v) : ch (c), val (v) {}

  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_RECV (ch, *val);
    CT_END ();
  }
};

static void
test_mixed_workload ()
{
  std::printf ("=== test 5: mixed workload ===\n");

  css::Scheduler sched;
  css::Channel ch;
  ch.reserve (4);
  Flag flag;

  bool sleeper_done = false;
  bool waiter_done = false;
  int chan_val = -1;

  sched.spawn<MixSleeper> (5, &sleeper_done);
  sched.spawn<MixWaiter> (flag, &waiter_done);
  sched.spawn<MixSender> (ch, &chan_val);
  sched.spawn<MixReceiver> (ch, &chan_val);
  for (int i = 0; i < 3; ++i)
    sched.spawn<MixYielder> (10);

  // fire the flag after some ticks via a delayed coroutine
  struct FireFlag : css::Coroutine
  {
    Flag *f;
    FireFlag (Flag *p) : f (p) {}
    css::Status
    run (css::Scheduler &sched)
    {
      CT_BEGIN ();
      CT_SLEEP (3);
      f->ready = 1;
      CT_END ();
    }
  };
  sched.spawn<FireFlag> (&flag);

  sched.run ();

  CHECK (sleeper_done);
  CHECK (waiter_done);
  CHECK (chan_val == 42);
  std::printf ("  all mixed coroutines completed correctly\n");
}

// ── test 6: spawn → destroy churn ───────────────────────────────────

struct Ephemeral : css::Coroutine
{
  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (1);
    CT_END ();
  }
};

static void
test_spawn_destroy_churn ()
{
  std::printf ("=== test 6: spawn/destroy churn ===\n");

  css::Scheduler sched;
  auto t0 = std::chrono::steady_clock::now ();
  for (int wave = 0; wave < 5000; ++wave)
    {
      sched.spawn<Ephemeral> ();
      if (sched.ready.size () > 200)
        sched.step ();
    }
  sched.run ();
  auto t1 = std::chrono::steady_clock::now ();

  auto us = std::chrono::duration_cast<std::chrono::microseconds> (t1 - t0)
                .count ();
  CHECK (sched.alive_size () == 0);
  std::printf ("  5k churned coroutines in %lld µs (%.1f ns each)\n",
               static_cast<long long> (us), us * 1000.0 / 5000.0);
}

// ── test 7: deep pipeline ───────────────────────────────────────────

static const int EOS = -1;

struct PipeStage : css::Coroutine
{
  css::Channel &in;
  css::Channel &out;
  PipeStage (css::Channel &i, css::Channel &o) : in (i), out (o) {}

  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    for (;;)
      {
        int val;
        CT_RECV (in, val);
        if (val == EOS)
          {
            out.send (sched, EOS);
            break;
          }
        out.send (sched, val);
      }
    CT_END ();
  }
};

struct PipeSource : css::Coroutine
{
  css::Channel &out;
  int n;
  int sent = 0;
  PipeSource (css::Channel &o, int count) : out (o), n (count) {}

  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    while (sent < n)
      {
        out.send (sched, sent);
        ++sent;
        CT_SLEEP (0);
      }
    out.send (sched, EOS);
    CT_END ();
  }
};

struct PipeSink : css::Coroutine
{
  css::Channel &in;
  int expected;
  int last = -1;
  int got = 0;
  PipeSink (css::Channel &i, int n) : in (i), expected (n) {}

  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    while (got < expected)
      {
        int val;
        CT_RECV (in, val);
        CHECK (val == last + 1);
        last = val;
        ++got;
      }
    // drain EOS
    {
      int eos;
      CT_RECV (in, eos);
      CHECK (eos == EOS);
    }
    CT_END ();
  }
};

static void
test_deep_pipeline ()
{
  std::printf ("=== test 7: deep pipeline (50 stages) ===\n");

  css::Scheduler sched;
  std::vector<css::Channel> ch (51);
  for (auto &c : ch)
    c.reserve (8);

  sched.spawn<PipeSource> (ch[0], 100);
  for (int i = 0; i < 49; ++i)
    sched.spawn<PipeStage> (ch[i], ch[i + 1]);
  sched.spawn<PipeSink> (ch[49], 100);

  auto t0 = std::chrono::steady_clock::now ();
  sched.run ();
  auto t1 = std::chrono::steady_clock::now ();

  auto us = std::chrono::duration_cast<std::chrono::microseconds> (t1 - t0)
                .count ();
  CHECK (sched.alive_size () == 0);
  std::printf ("  50-stage pipeline (100 values) in %lld µs\n",
               static_cast<long long> (us));
}

// ── test 8: retire → slot correctness ───────────────────────────────

struct SlotChecker : css::Coroutine
{
  css::Scheduler *s;
  int id;
  SlotChecker (css::Scheduler *p, int i) : s (p), id (i) {}

  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (1);                   // retire after 1 tick
    CHECK (s->tasks[slot] == this); // slot must still point back to us
    CT_END ();
  }
};

static void
test_slot_correctness ()
{
  std::printf ("=== test 8: retire slot correctness ===\n");
  css::Scheduler sched;
  std::vector<SlotChecker *> ptrs;

  for (int i = 0; i < 500; ++i)
    {
      auto *c = sched.spawn<SlotChecker> (&sched, i);
      ptrs.push_back (c);
    }

  sched.run ();
  CHECK (sched.alive_size () == 0);
  std::printf ("  500 slot checks passed\n");
}

// ── test 9: RingQueue growth correctness ────────────────────────────

static void test_ring_queue() {
  std::printf("=== test 9: RingQueue growth ===\n");

  css::RingQueue rq;
  // Start small — push enough to trigger multiple grows
  for (int i = 0; i < 2000; ++i)
    rq.push(i);

  CHECK(rq.size() == 2000);

  // Pop all, verify order
  for (int i = 0; i < 2000; ++i) {
    int v = -1;
    CHECK(rq.pop(v));
    CHECK(v == i);
  }

  CHECK(rq.empty());
  CHECK(!rq.pop(*reinterpret_cast<int*>(0))); // pop empty must return false

  // Wrap-around correctness: push 500, pop 200, push 300
  for (int i = 0; i < 500; ++i)
    rq.push(i);
  for (int i = 0; i < 200; ++i) {
    int v = -1;
    CHECK(rq.pop(v));
    CHECK(v == i);
  }
  for (int i = 500; i < 800; ++i)
    rq.push(i);
  for (int i = 200; i < 800; ++i) {
    int v = -1;
    CHECK(rq.pop(v));
    CHECK(v == i);
  }
  CHECK(rq.empty());

  std::printf("  RingQueue growth + wrap-around OK\n");
}
// ── test 10: zero-delay sleep / edge cases ──────────────────────────

struct ZeroSleeper : css::Coroutine
{
  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (0); // should be a no-op, return CT_READY
    CT_END ();
  }
};

struct NegativeSleeper : css::Coroutine
{
  css::Status
  run (css::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (-5); // treated as 0, no-op
    CT_END ();
  }
};

static void
test_edge_cases ()
{
  std::printf ("=== test 10: edge cases ===\n");

  css::Scheduler sched;
  sched.spawn<ZeroSleeper> ();
  sched.spawn<NegativeSleeper> ();
  sched.run ();

  CHECK (sched.alive_size () == 0);

  // retire nullptr
  sched.retire (nullptr); // must not crash

  // wake nullptr
  sched.wake (nullptr); // must not crash

  // sleep_current with zero delay: spawn + immediate sleep_current after
  // construct yields CT_READY
  struct Temp : css::Coroutine
  {
    css::Status
    run (css::Scheduler &)
    {
      CT_BEGIN ();
      CT_END ();
    }
  };
  auto *tmp = sched.spawn<Temp> ();
  sched.retire (tmp);

  // alive_count after manual retire
  auto *z = sched.spawn<ZeroSleeper> ();
  CHECK (sched.alive_size () == 1);
  sched.retire (z);
  CHECK (sched.alive_size () == 0);

  std::printf ("  edge cases passed\n");
}

// ── main ─────────────────────────────────────────────────────────────

int
main ()
{
  std::printf ("╔══════════════════════════════════╗\n");
  std::printf ("║  CoSchedSIMD — Stress Test       ║\n");
  std::printf ("╚══════════════════════════════════╝\n\n");

  test_mass_spawn ();
  test_precision_sleep ();
  test_simd_lanes ();
  test_channel_flood ();
  test_mixed_workload ();
  test_spawn_destroy_churn ();
  test_deep_pipeline ();
  test_slot_correctness ();
  test_ring_queue ();
  test_edge_cases ();

  std::printf ("\n── done: %d checks, %d failures ──\n", g_checks, g_fails);
  return g_fails ? 1 : 0;
}
