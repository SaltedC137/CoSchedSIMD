// 04_wait_until.cpp — polling a shared flag
#include "../libccss.hpp"
#include <cstdio>

struct Flag
{
  bool ready = false;
};

struct Waiter : ccss::Coroutine
{
  Flag &flag;

  Waiter (Flag &f) : flag (f) {}

  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    std::printf ("[waiter] waiting for flag…\n");
    CT_WAIT_UNTIL (flag.ready);
    std::printf ("[waiter] flag set at tick %lld\n",
                 static_cast<long long> (sched.now ()));
    CT_END ();
  }
};

struct Signaller : ccss::Coroutine
{
  Flag &flag;

  Signaller (Flag &f) : flag (f) {}

  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    CT_SLEEP (4); // delay the signal
    std::printf ("[signaller] setting flag\n");
    flag.ready = true;
    CT_END ();
  }
};

int
main ()
{
  Flag flag;
  ccss::Scheduler sched;
  sched.spawn<Waiter> (flag);
  sched.spawn<Signaller> (flag);
  sched.run ();
}
