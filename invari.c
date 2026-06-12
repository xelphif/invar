/*
 * invari.c - simple testing framework
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "invari.h"

#define XLOG_VISIBLE XLOG_TRACE

enum xlog_level {
  XLOG_TRACE,
  XLOG_DEBUG,
  XLOG_INFO,
  XLOG_WARN,
  XLOG_ERROR,
  XLOG_FATAL,
};

static char *xlog_level_str[] = {
    [XLOG_TRACE] = "TRACE", [XLOG_DEBUG] = "DEBUG", [XLOG_INFO] = "INFO",
    [XLOG_WARN] = "WARN",   [XLOG_ERROR] = "ERROR", [XLOG_FATAL] = "FATAL",
};

// TODO: variadic / color
void xlog(enum xlog_level level, char *msg) {

  if (level < XLOG_VISIBLE)
    return;

  printf("[%s] %s", xlog_level_str[level], msg);
}

/*
 * Internal functions to create variants based on type.
 */
struct variant _wrap_int(int i) {
  return (struct variant){.T = VARIANT_INT, .i = i};
}
struct variant _wrap_float(float f) {
  return (struct variant){.T = VARIANT_FLOAT, .f = f};
}
struct variant _wrap_str(char *str) {
  return (struct variant){.T = VARIANT_STR, .str = str};
}
struct variant _wrap_ptr(void *ptr) {
  return (struct variant){.T = VARIANT_PTR, .ptr = ptr};
}

/*
 * Iterate sequence and reset after n iterations.
 */
#define seq_iter(s)                                                            \
  ((struct sequence){                                                          \
      .v = (s).fn((s).it), .fn = (s).fn, .n = (s).n, .it = (s).it + 1})
struct sequence seq_next(struct sequence s) {
  return s.n > s.it ? seq_iter(s) : seq(s.fn, s.n);
}

#define param(x, y) ((struct parameter){.name = (x), .v = (y)})

static char *result_str[] = {
    [RESULT_OK] = " OK ",
    [RESULT_FAIL] = "FAIL",
    [RESULT_SKIP] = "SKIP",
};

/*
 * Permute array of n indices into domain.
 *
 * TODO: Improve counting for both dimensions of domain.
 */
int tick(struct domain *d, int n, int *index) {
  for (int i = n - 1; i >= 0; i--) {
    if (!(d + i)->name)
      continue;

    struct domain *d_i = d + i;
    struct variant *v_n = d_i->v + index[i] + 1;

    if (v_n->T == VARIANT_NONE) {
      if (!i) {
        // If our first value reaches its final value, we are finished. Reset
        // all indices and return 1.
        for (int i = 0; i < n; i++)
          index[i] = 0;
        return 1;
      }

      index[i] = 0;
      continue;
    }

    index[i]++;
    break;
  }
  return 0;
}

/*
 * Get current time with nanosecond precision.
 */
double timer(void) {
  struct timespec ts;
  clockid_t id = CLOCK_MONOTONIC_RAW;

  clock_gettime(id, &ts);

  return (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
}

/*
 * Run function in separate process and wait.
 *
 * TODO: report with time elapsed and exist status so run_args can handle
 * printing
 */
struct result run(void (*fn)(struct parameter *), struct parameter *p) {

  int pipefd[2];

  if (pipe(pipefd))
    exit(EXIT_FAILURE);

  // Run and time test in separate process.
  pid_t cpid = fork();
  if (cpid == 0) {
    close(pipefd[0]);

    double start = timer();

    fn(p);

    double elapsed = timer() - start;

    if (write(pipefd[1], &elapsed, sizeof(double)) < 0)
      exit(EXIT_FAILURE);

    close(pipefd[1]);

    exit(EXIT_SUCCESS);
  }

  if (cpid == -1) {
    close(pipefd[0]);
    close(pipefd[1]);
    exit(EXIT_FAILURE);
  }

  close(pipefd[1]);

  int status;
  waitpid(cpid, &status, 0);

  struct result r;
  if (read(pipefd[0], &r.t_elapsed, sizeof(double)) == -1)
    exit(EXIT_FAILURE);

  if (WIFEXITED(status)) {
    r.rt = WEXITSTATUS(status) ? RESULT_FAIL : RESULT_OK;
  }

  if (WIFSIGNALED(status)) {
    r.rt = RESULT_FAIL;
  }

  return r;
}

void print_result(struct test t, struct result r) {
  printf("%s: %s, %.9fsec\n", t.name, result_str[r.rt], r.t_elapsed);
}

/*
 * Run a test with each set of paramters from a domain.
 */
int run_args(struct test t) {
  int n = 0;
  for (int i = 0; (t.d + i)->name; i++)
    n++;

  struct parameter p[n];
  int index[n];

  // Populate array of parameters based on domain.
  for (int i = 0; i < n; i++) {
    p[i] = param(t.d[i].name, wrap_none());
    index[i] = 0;
  }

  do {
    // Set each parameter to the appropriate index in domain.
    for (int i = 0; i < n; i++)
      p[i].v = *((t.d + i)->v + index[i]);

    print_result(t, run(t.fn, p));
  } while (!tick(t.d, n, index));

  return 0;
};
