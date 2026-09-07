// test_ringqueue.cpp - RingQueue unit tests: wraparound, growth, order, clear
#include "harness.hpp"

#include "../libccss.hpp"

using ccss::RingQueue;

static int
pop_expect (RingQueue &q, int expect)
{
  int v = -1;
  if (!q.pop (v))
    {
      return -2; // empty queue counts as mismatch
    }
  return v == expect ? expect : -1;
}

CCSS_TEST (ring_fifo_1000)
{
  RingQueue q;
  for (int i = 0; i < 1000; ++i)
    {
      q.push (i);
    }
  CHECK_EQ (q.size (), 1000u);
  for (int i = 0; i < 1000; ++i)
    {
      CHECK_EQ (pop_expect (q, i), i);
    }
  CHECK (q.empty ());
  CHECK_EQ (q.size (), 0u);
}

CCSS_TEST (ring_pop_empty)
{
  RingQueue q;
  int v = -123;
  CHECK (!q.pop (v));
  CHECK_EQ (v, -123); // output parameter must stay untouched
}

CCSS_TEST (ring_growth_over_capacity)
{
  RingQueue q;                 // starts empty; first push grows to 64
  for (int i = 0; i < 65; ++i) // 65 > 64 -> grows again
    {
      q.push (i);
    }
  for (int i = 0; i < 65; ++i)
    {
      CHECK_EQ (pop_expect (q, i), i);
    }
}

CCSS_TEST (ring_wraparound_keeps_fifo)
{
  RingQueue q;
  for (int i = 0; i < 40; ++i)
    {
      q.push (10 + i);
    }
  for (int i = 0; i < 20; ++i) // head advances to 20; data wraps around
    {
      CHECK_EQ (pop_expect (q, 10 + i), 10 + i);
    }
  for (int i = 0; i < 80; ++i)
    {
      q.push (50 + i); // grows and re-bases head to 0
    }
  for (int i = 0; i < 20; ++i)
    {
      CHECK_EQ (pop_expect (q, 30 + i), 30 + i);
    }
  for (int i = 0; i < 80; ++i)
    {
      CHECK_EQ (pop_expect (q, 50 + i), 50 + i);
    }
  CHECK (q.empty ());
}

CCSS_TEST (ring_reserve_no_grow)
{
  RingQueue q;
  q.reserve (8);
  CHECK (q.data.size () >= 8u);
  for (int i = 0; i < 8; ++i)
    {
      q.push (i);
    }
  CHECK_EQ (q.data.size (), 8u); // no growth triggered
  q.push (8);                    // first overflow -> 16
  CHECK (q.data.size () >= 16u);
  for (int i = 0; i < 9; ++i)
    {
      CHECK_EQ (pop_expect (q, i), i);
    }
}

CCSS_TEST (ring_grow_mid_head)
{
  RingQueue q;
  q.reserve (4);
  for (int i = 0; i < 4; ++i)
    {
      q.push (i);
    }
  CHECK (pop_expect (q, 0) == 0);
  CHECK (pop_expect (q, 1) == 1); // head now at 2
  q.push (10);
  q.push (11);
  q.push (12); // grows; old data [2,3,10,11,12] must stay in order
  const int expect[] = { 2, 3, 10, 11, 12 };
  for (int i = 0; i < 5; ++i)
    {
      CHECK_EQ (pop_expect (q, expect[i]), expect[i]);
    }
}

CCSS_TEST (ring_clear_reusable)
{
  RingQueue q;
  for (int i = 0; i < 100; ++i)
    {
      q.push (i);
    }
  q.clear ();
  CHECK (q.empty ());
  q.push (7);
  int v = -1;
  CHECK (q.pop (v));
  CHECK_EQ (v, 7);
  CHECK (q.empty ());
}

MAIN ()