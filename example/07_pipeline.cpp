// 07_pipeline.cpp — multi-stage pipeline with channels + sentinel shutdown
#include "../libccss.hpp"
#include <cstdio>

static const int EOS = -1; // end-of-stream sentinel

struct Generator : ccss::Coroutine
{
  ccss::Channel &out;
  int sent;
  int total;
  Generator (ccss::Channel &o, int n) : out (o), sent (0), total (n) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    while (sent < total)
      {
        ++sent;
        out.send (sched, sent);
        std::printf ("[gen] → %d\n", sent);
        CT_SLEEP (1);
      }
    out.send (sched, EOS); // signal end-of-stream
    std::printf ("[gen] sent EOS\n");
    CT_END ();
  }
};

struct Doubler : ccss::Coroutine
{
  ccss::Channel &in;
  ccss::Channel &out;
  Doubler (ccss::Channel &i, ccss::Channel &o) : in (i), out (o) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    for (;;)
      {
        int val;
        CT_RECV (in, val);
        if (val == EOS)
          {
            out.send (sched, EOS); // forward sentinel
            break;
          }
        int doubled = val * 2;
        out.send (sched, doubled);
        std::printf ("[dbl] %d → %d\n", val, doubled);
      }
    std::printf ("[dbl] got EOS, shutting down\n");
    CT_END ();
  }
};

struct Printer : ccss::Coroutine
{
  ccss::Channel &in;
  Printer (ccss::Channel &i) : in (i) {}
  ccss::Status
  run (ccss::Scheduler &sched)
  {
    CT_BEGIN ();
    for (;;)
      {
        int val;
        CT_RECV (in, val);
        if (val == EOS)
          {
            break;
          }
        std::printf ("[prn] result: %d\n", val);
      }
    std::printf ("[prn] got EOS, pipeline complete\n");
    CT_END ();
  }
};

int
main ()
{
  ccss::Scheduler sched;

  ccss::Channel gen_to_dbl;
  ccss::Channel dbl_to_prn;
  gen_to_dbl.reserve (4);
  dbl_to_prn.reserve (4);

  sched.spawn<Generator> (gen_to_dbl, 4);
  sched.spawn<Doubler> (gen_to_dbl, dbl_to_prn);
  sched.spawn<Printer> (dbl_to_prn);

  sched.run ();
}
