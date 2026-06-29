// 08_phases.cpp — multi-phase state machine via Coroutine
#include "../libcss.hpp"
#include <cstdio>

struct Phaser : css::Coroutine {
  css::Status run(css::Scheduler &sched) {
    CT_BEGIN();

    // PHASE 1: wait 3 ticks, then do work
    std::printf("[phase1] waiting…\n");
    CT_SLEEP(3);
    std::printf("[phase1] work done at tick %lld\n",
                static_cast<long long>(sched.now()));

    // PHASE 2: yield twice, then transition
    std::printf("[phase2] yielding twice\n");
    CT_YIELD();
    CT_YIELD();
    std::printf("[phase2] complete\n");

    // PHASE 3: wait for a condition
    CT_SLEEP(2);
    std::printf("[phase3] finished at tick %lld\n",
                static_cast<long long>(sched.now()));

    CT_END();
  }
};

int main() {
  css::Scheduler sched;
  sched.spawn<Phaser>();
  sched.run();
}
