/**
 * Copyright (c) 2023 nicegraf contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#if !defined(NT_IMPL)
#pragma once
#endif

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32) || defined(_WIN64)
#define NT_THREADLOCAL __declspec(thread)
#else
#define NT_THREADLOCAL __thread
#endif

#if defined(NT_BREAK_ON_ASSERT_FAIL)
#if !defined(_WIN32) && !defined(_WIN64)
#include <signal.h>
#define NT_ASSERT(condition)              \
  do {                                    \
    if (!(condition)) { raise(SIGTRAP); } \
    }while (0)
#else
#define NT_ASSERT(condition)              \
  do {                                    \
    if (!(condition)) { __debugbreak(); } \
  } while (0)
#endif
#else
#define NT_ASSERT(condition)                                                 \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(                                                                 \
          stderr,                                                              \
          "assertion failed\n\tcode:\t\"%s\"\n\tfile:\t\"%s\"\n\tline:\t%d\n", \
          #condition,                                                          \
          __FILE__,                                                            \
          __LINE__);                                                           \
      if (nt_internal_mainthread_flag) {                                       \
        longjmp(nt_internal_jmpbuf, 1);                                        \
      } else {                                                                 \
        fprintf(stderr, "Assertion failure on non-main thread. Aborting.\n");  \
        abort();                                                               \
      }                                                                        \
    }                                                                          \
  } while (0)
#endif

#define NT_TESTCASE(name)                                             \
    if ((setjmp(nt_internal_jmpbuf) == 0 &&                           \
         nt_internal_handle_test_start(#name, test_suite_context)) || \
        nt_internal_handle_test_failure(#name, test_suite_context))   \

#define NT_TESTSUITE \
  void nt_internal_test_suite_main(nt_internal_test_suite_context* test_suite_context)

typedef struct nt_internal_test_suite_context {
  uint32_t failed_test_cases;
  uint32_t total_test_cases;
} nt_internal_test_suite_context;

#if !defined(NT_IMPL)
static int nt_internal_handle_test_failure(
    const char*                     test_case_name,
    nt_internal_test_suite_context* test_suite_context) {
  fprintf(stderr, "test case \"%s\" failed.\n", test_case_name);
  test_suite_context->failed_test_cases++;
  return 0;
}

static int nt_internal_handle_test_start(
    const char*                     test_case_name,
    nt_internal_test_suite_context* test_suite_context) {
  fprintf(stderr, "\nrunning test case: \"%s\"\n", test_case_name);
  test_suite_context->total_test_cases++;
  return 1;
}
#endif

extern NT_THREADLOCAL bool nt_internal_mainthread_flag;
extern jmp_buf             nt_internal_jmpbuf;
void nt_internal_test_suite_main(nt_internal_test_suite_context* test_suite_context);

#if defined(NT_SELF_TEST)

void failfoo() {
  NT_ASSERT(false);
}

NT_TESTSUITE {
  NT_TESTCASE(succesful - tc1) {
    NT_ASSERT(true);
  }

  NT_TESTCASE(failed - tc1) {
    NT_ASSERT(false);
  }

  NT_TESTCASE(failed - tc2) {
    failfoo();
  }
}

#endif

#if defined(NT_IMPL)

NT_THREADLOCAL bool nt_internal_mainthread_flag;
jmp_buf             nt_internal_jmpbuf;

int main(int argc, char* argv[]) {
  (void)argv;
  (void)argc;
  nt_internal_test_suite_context ctx;
  ctx.failed_test_cases       = 0u;
  ctx.total_test_cases        = 0u;
  nt_internal_mainthread_flag = true;
  fprintf(stderr, "running test suite...\n");
  nt_internal_test_suite_main(&ctx);
  fprintf(
      stderr,
      "\nfinished test suite, with %d failed test cases (out of %d total test cases)\n",
      ctx.failed_test_cases,
      ctx.total_test_cases);
  return ctx.failed_test_cases == 0 ? 0 : 1;
}
#endif