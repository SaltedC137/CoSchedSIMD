# libcss — Cooperative Coroutine Scheduler with SIMD

A single-header, zero-dependency C++17 cooperative coroutine scheduler. Uses AVX2 SIMD to accelerate sleep-timer management (8 lanes of int32 decrement + wakeup compaction per cycle). Coroutines are stackless via Duff's Device — no `setjmp`/`longjmp`, no assembly, no allocations per context-switch.

## Quick Look

```cpp
#include "libcss.hpp"

struct Blinker : css::Coroutine {
  css::Status run(css::Scheduler &sched) {
    CT_BEGIN();               // opens the state-machine switch
    for (;;) {
      CT_SLEEP(3);           // suspend for 3 ticks
      printf("on\n");
      CT_SLEEP(3);
      printf("off\n");
    }
    CT_END();
  }
};

int main() {
  css::Scheduler sched;
  sched.spawn<Blinker>();
  sched.run();               // loops until all coroutines are dead
}
```

## Features

- **AVX2 sleep manager** — batches 8 delays per SIMD lane; autodetects `__AVX2__` at compile time, falls back to portable scalar otherwise
- **Duff's Device coroutines** — `CT_BEGIN / YIELD / SLEEP / WAIT_UNTIL / RECV / END` macros; each coroutine stays on the caller's stack
- **Channels** — ring-buffer FIFO queues with waiter wakeup for intra-scheduler communication
- **Header-only** — drop `libcss.hpp` into your project, done

## API

| Macro / Method | Purpose |
|---|---|
| `CT_BEGIN()` | Open coroutine state machine |
| `CT_YIELD()` | Yield to next ready coroutine |
| `CT_SLEEP(n)` | Suspend for *n* ticks |
| `CT_WAIT_UNTIL(cond)` | Busy-wait until condition holds |
| `CT_RECV(ch, var)` | Block until a value arrives on channel `ch` |
| `CT_END()` | Terminate the coroutine |
| `sched.spawn<T>(args...)` | Create and register a coroutine |
| `sched.run()` | Run the scheduler to exhaustion |
| `sched.step()` | Single tick (sleep → run → cleanup) |

## Build

```sh
mkdir build && cd build
cmake .. -DCO_SCHED_BUILD_EXAMPLES=ON  # optional examples
make
```

Or just `#include "libcss.hpp"` — no compilation needed.

## Caveats

- **No preemption** — a coroutine runs until it explicitly yields or sleeps; long computation blocks everyone.
- **No automatic variables across suspension** — use member fields for state that must survive across `CT_YIELD` / `CT_SLEEP`.
- **Single-threaded** — the scheduler is not thread-safe; serialize external events via channels.

## License

MIT
