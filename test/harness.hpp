#pragma once

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace ccss_test
{

struct Case
{
  const char *name;
  void (*fn) ();
};

inline std::vector<Case> &
registry ()
{
  static std::vector<Case> all;
  return all;
}

inline int fail_count = 0;

inline void
report_failure (const char *file, int line, const std::string &msg)
{
  ++fail_count;
  std::fprintf (stderr, "    [ASSERT] %s:%d: %s\n", file, line, msg.c_str ());
}

struct Registrar
{
  Registrar (const char *name, void (*fn) ())
  {
    registry ().push_back ({ name, fn });
  }
};

template <class A, class B>
void
check_relation (const char *file, int line, const char *as, const char *bs,
                const A &a, const B &b, bool expect_equal)
{
  if ((a == b) != expect_equal)
    {
      std::ostringstream os;
      os << as << " " << (expect_equal ? "!=" : "==") << " " << bs << " (" << a
         << " vs " << b << ")";
      report_failure (file, line, os.str ());
    }
}

inline int
run_all (int argc, char **argv)
{
  const char *filter = (argc > 1) ? argv[1] : "";
  int total = 0;
  int passed = 0;

  for (const Case &c : registry ())
    {
      if (filter[0] != '\0'
          && std::string (c.name).find (filter) == std::string::npos)
        {
          continue;
        }
      ++total;
      const int before = fail_count;
      std::fprintf (stderr, "[ RUN  ] %s\n", c.name);
      c.fn ();
      if (fail_count == before)
        {
          ++passed;
          std::fprintf (stderr, "[  OK  ] %s\n", c.name);
        }
      else
        {
          std::fprintf (stderr, "[FAILED] %s (%d assertion(s))\n", c.name,
                        fail_count - before);
        }
    }

  std::fprintf (stderr, "\n%d/%d cases passed\n", passed, total);
  return (fail_count == 0 && total > 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace ccss_test

#define CCSS_TEST(name)                                                       \
  static void name ();                                                        \
  static ::ccss_test::Registrar ccss_reg_##name (#name, &(name));             \
  static void name ()

#define CHECK(cond)                                                           \
  do                                                                          \
    {                                                                         \
      if (!(cond))                                                            \
        {                                                                     \
          ::ccss_test::report_failure (__FILE__, __LINE__,                    \
                                       "CHECK(" #cond ")");                   \
          return;                                                             \
        }                                                                     \
    }                                                                         \
  while (0)

#define CHECK_EQ(a, b)                                                        \
  ::ccss_test::check_relation (__FILE__, __LINE__, #a, #b, (a), (b), true)

#define CHECK_NE(a, b)                                                        \
  ::ccss_test::check_relation (__FILE__, __LINE__, #a, #b, (a), (b), false)

#define MAIN()                                                                \
  int main (int argc, char **argv)                                            \
  {                                                                           \
    return ::ccss_test::run_all (argc, argv);                                 \
  }
