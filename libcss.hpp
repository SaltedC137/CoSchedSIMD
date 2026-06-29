#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CTICK_FORCE_INLINE inline __attribute__ ((always_inline))
#define CTICK_NOINLINE __attribute__ ((noinline))
#define CTICK_HOT __attribute__ ((hot))
#define CTICK_LIKELY(x) __builtin_expect (!!(x), 1)
#define CTICK_UNLIKELY(x) __builtin_expect (!!(x), 0)
#define CTICK_PREFETCH_R(p) __builtin_prefetch ((p), 0, 1)
#define CSS_FALLTHROUGH __attribute__ ((fallthrough))
#elif defined(_MSC_VER)
#define CTICK_FORCE_INLINE __forceinline
#define CTICK_NOINLINE __declspec (noinline)
#define CTICK_HOT
#define CTICK_LIKELY(x) (x)
#define CTICK_UNLIKELY(x) (x)
#define CTICK_PREFETCH_R(p)
#define CSS_FALLTHROUGH __fallthrough
#else
#define CTICK_FORCE_INLINE inline
#define CTICK_NOINLINE
#define CTICK_HOT
#define CTICK_LIKELY(x) (x)
#define CTICK_UNLIKELY(x) (x)
#define CTICK_PREFETCH_R(p)
#define CSS_FALLTHROUGH (void)0
#endif

namespace css
{

struct Scheduler;
struct Coroutine;

enum Status : uint8_t
{
  CT_READY = 0,
  CT_SLEEPING = 1,
  CT_WAITING = 2,
  CT_DEAD = 3
};

struct Coroutine
{
  typedef Status (*RunFn) (Coroutine *, Scheduler &);
  typedef void (*DestroyFn) (Coroutine *);

  int32_t pc;
  Status status;
  RunFn run_fn;
  DestroyFn destroy_fn;
  std::size_t slot;

  CTICK_FORCE_INLINE
  Coroutine ()
      : pc (0), status (CT_READY), run_fn (0), destroy_fn (0), slot (0)
  {
  }

  CTICK_FORCE_INLINE bool
  done () const
  {
    return status == CT_DEAD;
  }

protected:
  ~Coroutine () {}
};

template <class T>
CTICK_FORCE_INLINE Status
invoke_coroutine (Coroutine *c, Scheduler &s)
{
  return static_cast<T *> (c)->run (s);
}

template <class T>
CTICK_FORCE_INLINE void
destroy_coroutine (Coroutine *c)
{
  delete static_cast<T *> (c);
}

struct RingQueue
{
  std::vector<int> data;
  std::size_t head;
  std::size_t tail;
  std::size_t count;

  RingQueue () : head (0), tail (0), count (0) {}

  CTICK_FORCE_INLINE bool
  empty () const
  {
    return count == 0;
  }

  CTICK_FORCE_INLINE std::size_t
  size () const
  {
    return count;
  }

  void
  reserve (std::size_t n)
  {
    if (data.size () >= n)
      {
        return;
      }
    std::vector<int> nd (n ? n : 1);
    std::size_t old = data.size ();
    for (std::size_t i = 0; i < count; ++i)
      {
        nd[i] = old ? data[(head + i) % old] : 0;
      }
    data.swap (nd);
    head = 0;
    tail = count;
  }

  void
  grow ()
  {
    std::size_t old = data.size ();
    std::size_t cap = old ? old * 2 : 64;
    std::vector<int> nd (cap);
    for (std::size_t i = 0; i < count; ++i)
      {
        nd[i] = old ? data[(head + i) % old] : 0;
      }
    data.swap (nd);
    head = 0;
    tail = count;
  }

  CTICK_FORCE_INLINE void
  push (int v)
  {
    if (CTICK_UNLIKELY (count == data.size ()))
      {
        grow ();
      }
    data[tail] = v;
    ++tail;
    if (CTICK_UNLIKELY (tail == data.size ()))
      {
        tail = 0;
      }
    ++count;
  }

  CTICK_FORCE_INLINE bool
  pop (int &v)
  {
    if (CTICK_UNLIKELY (count == 0))
      {
        return false;
      }

    v = data[head];
    ++head;

    if (CTICK_UNLIKELY (head == data.size ()))
      {
        head = 0;
      }

    --count;
    return true;
  }

  CTICK_FORCE_INLINE void
  clear ()
  {
    head = 0;
    tail = 0;
    count = 0;
  }
};

struct SleepManager
{
  std::vector<int32_t> delays;
  std::vector<Coroutine *> tasks;

  CTICK_FORCE_INLINE void
  reserve (std::size_t n)
  {
    delays.reserve (n);
    tasks.reserve (n);
  }

  CTICK_FORCE_INLINE bool
  empty () const
  {
    return tasks.empty ();
  }

  CTICK_FORCE_INLINE std::size_t
  size () const
  {
    return tasks.size ();
  }

  CTICK_FORCE_INLINE void
  add (Coroutine *c, int32_t delay)
  {
    delays.push_back (delay);
    tasks.push_back (c);
  }

  CTICK_HOT void
  tick (std::vector<Coroutine *> &ready)
  {
    std::size_t n = delays.size ();

    if (CTICK_UNLIKELY (n == 0))
      {
        return;
      }

#if defined(__AVX2__)
    std::size_t i = 0;
    __m256i one = _mm256_set1_epi32 (1);

    for (; i + 8 <= n; i += 8)
      {
        if (CTICK_LIKELY (i + 16 < n))
          {
            CTICK_PREFETCH_R (&delays[i + 16]);
          }
        __m256i d = _mm256_loadu_si256 (
            reinterpret_cast<const __m256i *> (&delays[i]));
        d = _mm256_sub_epi32 (d, one);
        _mm256_storeu_si256 (reinterpret_cast<__m256i *> (&delays[i]), d);
      }

    for (; i < n; ++i)
      {
        --delays[i];
      }

    std::size_t keep = 0;
    i = 0;

    for (; i + 8 <= n;)
      {
        __m256i d = _mm256_loadu_si256 (
            reinterpret_cast<const __m256i *> (&delays[i]));
        __m256i cmp = _mm256_cmpgt_epi32 (one, d);
        int mask = _mm256_movemask_ps (_mm256_castsi256_ps (cmp));

        if (CTICK_LIKELY (mask == 0))
          {
            if (keep != i)
              {
                delays[keep + 0] = delays[i + 0];
                delays[keep + 1] = delays[i + 1];
                delays[keep + 2] = delays[i + 2];
                delays[keep + 3] = delays[i + 3];
                delays[keep + 4] = delays[i + 4];
                delays[keep + 5] = delays[i + 5];
                delays[keep + 6] = delays[i + 6];
                delays[keep + 7] = delays[i + 7];

                tasks[keep + 0] = tasks[i + 0];
                tasks[keep + 1] = tasks[i + 1];
                tasks[keep + 2] = tasks[i + 2];
                tasks[keep + 3] = tasks[i + 3];
                tasks[keep + 4] = tasks[i + 4];
                tasks[keep + 5] = tasks[i + 5];
                tasks[keep + 6] = tasks[i + 6];
                tasks[keep + 7] = tasks[i + 7];
              }

            i += 8;
            keep += 8;
            continue;
          }

        if (CTICK_UNLIKELY (mask == 255))
          {
            for (int j = 0; j < 8; ++j)
              {
                Coroutine *c = tasks[i + static_cast<std::size_t> (j)];
                c->status = CT_READY;
                ready.push_back (c);
              }

            i += 8;
            continue;
          }

        for (int j = 0; j < 8; ++j, ++i)
          {
            Coroutine *c = tasks[i];

            if (mask & (1 << j))
              {
                c->status = CT_READY;
                ready.push_back (c);
              }
            else
              {
                delays[keep] = delays[i];
                tasks[keep] = c;
                ++keep;
              }
          }
      }

    for (; i < n; ++i)
      {
        Coroutine *c = tasks[i];

        if (delays[i] <= 0)
          {
            c->status = CT_READY;
            ready.push_back (c);
          }
        else
          {
            delays[keep] = delays[i];
            tasks[keep] = c;
            ++keep;
          }
      }

    delays.resize (keep);
    tasks.resize (keep);
#else
    std::size_t keep = 0;

    for (std::size_t i = 0; i < n; ++i)
      {
        int32_t d = delays[i] - 1;
        Coroutine *c = tasks[i];

        if (d <= 0)
          {
            c->status = CT_READY;
            ready.push_back (c);
          }
        else
          {
            delays[keep] = d;
            tasks[keep] = c;
            ++keep;
          }
      }

    delays.resize (keep);
    tasks.resize (keep);
#endif
  }
};

struct Channel
{
  RingQueue q;
  std::vector<Coroutine *> waiters;

  CTICK_FORCE_INLINE void
  reserve (std::size_t n)
  {
    q.reserve (n);
    waiters.reserve (n);
  }

  CTICK_FORCE_INLINE bool
  recv (int &v)
  {
    return q.pop (v);
  }

  CTICK_FORCE_INLINE bool
  empty () const
  {
    return q.empty ();
  }

  CTICK_FORCE_INLINE std::size_t
  size () const
  {
    return q.size ();
  }

  void send (Scheduler &sched, int v);
  Status wait (Scheduler &sched, Coroutine *c);
};

struct Scheduler
{
  std::vector<Coroutine *> ready;
  std::vector<Coroutine *> runq;
  std::vector<Coroutine *> tasks;
  std::vector<Coroutine *> dead_list;
  SleepManager sleep_mgr;
  int64_t tick_count;
  std::size_t alive_count;

  Scheduler () : tick_count (0), alive_count (0) {}

  ~Scheduler ()
  {
    cleanup ();

    for (std::size_t i = 0; i < tasks.size (); ++i)
      {
        Coroutine *c = tasks[i];

        if (c && c->destroy_fn)
          {
            c->destroy_fn (c);
          }
      }

    tasks.clear ();
  }

  Scheduler (const Scheduler &) = delete;
  Scheduler &operator= (const Scheduler &) = delete;

  CTICK_FORCE_INLINE void
  reserve (std::size_t n)
  {
    ready.reserve (n);
    runq.reserve (n);
    tasks.reserve (n);
    dead_list.reserve (n > 16 ? n >> 4 : 16);
    sleep_mgr.reserve (n);
  }

  template <class T, class... Args>
  T *
  spawn (Args &&...args)
  {
    static_assert (std::is_base_of<Coroutine, T>::value,
                   "T must derive from Coroutine");

    T *t = new T (std::forward<Args> (args)...);
    init<T> (t);
    return t;
  }

  template <class T>
  CTICK_FORCE_INLINE void
  init (T *t)
  {
    t->pc = 0;
    t->status = CT_READY;
    t->run_fn = &invoke_coroutine<T>;
    t->destroy_fn = &destroy_coroutine<T>;
    t->slot = tasks.size ();

    tasks.push_back (t);
    ready.push_back (t);
    ++alive_count;
  }

  CTICK_FORCE_INLINE Status
  sleep_current (Coroutine *c, int32_t delay)
  {
    if (CTICK_UNLIKELY (delay <= 0))
      {
        return CT_READY;
      }

    c->status = CT_SLEEPING;
    sleep_mgr.add (c, delay);
    return CT_SLEEPING;
  }

  CTICK_FORCE_INLINE void
  wake (Coroutine *c)
  {
    if (CTICK_LIKELY (c && c->status != CT_DEAD && c->status != CT_READY))
      {
        c->status = CT_READY;
        ready.push_back (c);
      }
  }

  CTICK_FORCE_INLINE bool
  alive () const
  {
    return alive_count != 0;
  }

  CTICK_FORCE_INLINE std::size_t
  alive_size () const
  {
    return alive_count;
  }

  CTICK_FORCE_INLINE int64_t
  now () const
  {
    return tick_count;
  }

  CTICK_FORCE_INLINE void
  retire (Coroutine *c)
  {
    if (CTICK_UNLIKELY (!c))
      {
        return;
      }

    c->status = CT_DEAD;

    std::size_t idx = c->slot;
    std::size_t last = tasks.size () - 1;

    if (idx != last)
      {
        Coroutine *moved = tasks[last];
        tasks[idx] = moved;
        moved->slot = idx;
      }

    tasks.pop_back ();
    dead_list.push_back (c);
    --alive_count;
  }

  CTICK_HOT void
  step ()
  {
    sleep_mgr.tick (ready);

    runq.clear ();
    runq.swap (ready);

    for (std::size_t i = 0; i < runq.size (); ++i)
      {
        Coroutine *c = runq[i];

        if (CTICK_UNLIKELY (c->status != CT_READY))
          {
            continue;
          }

        Status s = c->run_fn (c, *this);

        if (s == CT_READY)
          {
            c->status = CT_READY;
            ready.push_back (c);
          }
        else if (s == CT_DEAD)
          {
            retire (c);
          }
        else
          {
            c->status = s;
          }
      }

    cleanup ();
    ++tick_count;
  }

  void
  run ()
  {
    while (alive ())
      {
        step ();
      }
  }

  CTICK_FORCE_INLINE void
  cleanup ()
  {
    for (std::size_t i = 0; i < dead_list.size (); ++i)
      {
        Coroutine *c = dead_list[i];

        if (c && c->destroy_fn)
          {
            c->destroy_fn (c);
          }
      }

    dead_list.clear ();
  }
};

CTICK_FORCE_INLINE void
Channel::send (Scheduler &sched, int v)
{
  q.push (v);

  if (CTICK_UNLIKELY (!waiters.empty ()))
    {
      Coroutine *c = waiters.back ();
      waiters.pop_back ();
      sched.wake (c);
    }
}

CTICK_FORCE_INLINE Status
Channel::wait (Scheduler & /*unused*/, Coroutine *c)
{
  c->status = CT_WAITING;
  waiters.push_back (c);
  return CT_WAITING;
}

} // namespace css

#define CT_BEGIN()                                                            \
  switch (this->pc)                                                           \
    {                                                                         \
    case 0:

#define CT_YIELD() CT_YIELD_IMPL (__LINE__)
#define CT_YIELD_IMPL(n)                                                      \
  do                                                                          \
    {                                                                         \
      this->pc = n;                                                           \
      return ::css::CT_READY;                                                 \
    case n:;                                                                  \
    }                                                                         \
  while (0)

#define CT_SLEEP(ticks) CT_SLEEP_IMPL (ticks, __LINE__)
#define CT_SLEEP_IMPL(ticks, n)                                               \
  do                                                                          \
    {                                                                         \
      if ((ticks) > 0)                                                        \
        {                                                                     \
          this->pc = n;                                                       \
          return sched.sleep_current (this, static_cast<int32_t> (ticks));    \
        }                                                                     \
      CSS_FALLTHROUGH;                                                        \
    case n:;                                                                  \
    }                                                                         \
  while (0)

#define CT_WAIT_UNTIL(cond) CT_WAIT_UNTIL_IMPL (cond, __LINE__)
#define CT_WAIT_UNTIL_IMPL(cond, n)                                           \
  do                                                                          \
    {                                                                         \
      if (!(cond))                                                            \
        {                                                                     \
          this->pc = n;                                                       \
          return ::css::CT_READY;                                             \
        case n:                                                               \
          if (!(cond))                                                        \
            return ::css::CT_READY;                                           \
        }                                                                     \
    }                                                                         \
  while (0)

#define CT_RECV(ch, out) CT_RECV_IMPL (ch, out, __LINE__)
#define CT_RECV_IMPL(ch, out, n)                                              \
  do                                                                          \
    {                                                                         \
      if (!(ch).recv (out))                                                   \
        {                                                                     \
          this->pc = n;                                                       \
          return (ch).wait (sched, this);                                     \
        case n:                                                               \
          if (!(ch).recv (out))                                               \
            {                                                                 \
              this->pc = n;                                                   \
              return (ch).wait (sched, this);                                 \
            }                                                                 \
        }                                                                     \
    }                                                                         \
  while (0)

#define CT_END()                                                              \
  }                                                                           \
  return ::css::CT_DEAD
