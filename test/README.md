# Test Suite

Zero third-party dependencies (bundled lightweight harness), integrated with
CTest, ASan + UBSan by default (auto-disabled with a warning when the runtime
libraries are missing).

## Build & Run

```sh
cmake -S . -B build-test -DCO_SCHED_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-test -j"$(nproc)"
ctest --test-dir build-test --output-on-failure
```

Run a single binary directly (supports name-prefix filtering):

```sh
./build-test/test/ccss_test_scheduler          # everything
./build-test/test/ccss_test_scheduler retire   # retire-related cases only
```

Options: pass `-DCCSS_TEST_SANITIZE=OFF` to build without sanitizers
(debugging only).

## Layout

| File | Coverage |
|---|---|
| `test_ringqueue` | wraparound, growth, order preservation, clear, empty pop |
| `test_sleepmanager` | expiry order, removal, negative/zero, INT32_MAX edge, bulk order |
| `test_sleepmanager_scalar` | same assertions on the non-AVX2 path (dual-path consistency) |
| `test_channel` | FIFO, bounded/unbounded, waiter lifecycle, LIFO wakeup, guards |
| `test_scheduler` | round-robin, tick alignment, delay clamp, retire/wake semantics, destruction |
| `test_macros` | pc progress, sleep(0) skip, WAIT_UNTIL, multi-value RECV |
| `test_stress` | 1000 coroutines, 500-round ping-pong, bounded queue, mass retire |

## Regression Cases

The scheduler file carries two cases derived from the code review, each
targeting one historical defect:

| Case | Defect | Pre-fix symptom |
|---|---|---|
| `scheduler_retire_ready_race` | #1 `retire()` left entries in `ready` | ASan: use-after-free; or `runs > 2` |
| `scheduler_sleep_current_clamps` | #2 `CT_SLEEP` macro truncated to int32 | oversized delay woke on the first tick |

The header already carries the fixes (see `Scheduler::retire` and
`CT_SLEEP_IMPL`), so both cases must pass; they fail if the fix is reverted.
Run the suite under sanitizers to harden the #1 case against non-deterministic
heap behavior.

## Macro Conventions (library constraints)

- `CT_*` case labels cannot jump past a **local declaration with an
  initializer** (`int i = 0;` fails to compile; use a member counter or a
  `for (; cond; ++)` loop);
- two `CT_YIELD`/`CT_SLEEP`/`CT_RECV` on the same line collide via
  `__LINE__` and fail to compile.