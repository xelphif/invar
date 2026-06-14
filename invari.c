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

#define ITER_DEFAULT 100

/*
 * Internal functions to create variants based on type.
 */
struct variant _wrap_int(int i) {
  return (struct variant){.T = VARIANT_INT, .i = i};
}
struct variant _wrap_double(double d) {
  return (struct variant){.T = VARIANT_DOUBLE, .d = d};
}
struct variant _wrap_str(char *str) {
  return (struct variant){.T = VARIANT_STR, .str = str};
}
struct variant _wrap_ptr(void *ptr) {
  return (struct variant){.T = VARIANT_PTR, .ptr = ptr};
}

/*
 * Print variant properly.
 */
void variant_print(struct variant v) {
  switch (v.T) {
  case VARIANT_INT:
    printf("%d", v.i);
    break;

  case VARIANT_DOUBLE:
    printf("%f", v.d);
    break;

  case VARIANT_STR:
    printf("\"%s\"", v.str);
    break;

  case VARIANT_PTR:
    printf("%p", v.ptr);
    break;

  default:
    printf("<none>");
  }
}

#define instance_init(x, y) ((struct instance){.name = (x), .v = (y)})

/*
 * Search for a variant by instance name.
 */
struct variant instance_get(struct instance *A, char *k) {
  for (int i = 0; (A + i)->name; i++) {
    if (!strcmp((A + i)->name, k))
      return (A + i)->v;
  }

  return (struct variant){0};
}

static char *result_str[] = {
    [false] = "FAIL",
    [true] = "OK",
};

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
 * Timing statistics helper functions
 */
static double mean(double old, double t, int n) {
  return ((old * n) + t) / (n + 1);
}
static void minmax(double *min, double *max, double n) {
  if (*min == 0.0 || *min > n)
    *min = n;
  *max = *max < n ? n : *max;
}

/*
 * Show results of a property test.
 */
void result_print(struct property *prop, struct report *r, bool color) {
  int passed = r->ac[true];
  int failed = r->ac[false];

  char *reset = color ? "\e[0m" : "";
  char *bold = color ? "\e[1m" : "";
  char *accent = color ? (!failed ? "\e[1;32m" : "\e[1;31m") : "";

  printf("\n"
         "  %s%-4s%s %s%s%s\n"
         "       %d out of %d passed\n"
         "       mean:  %.9fs\n"
         "       range: [%.9fs, %.9fs]\n",
         accent, result_str[r->ok], reset, bold, prop->name, reset, passed,
         passed + failed, r->t.mean, r->t.min, r->t.max);

  if (!r->n)
    return;

  printf("\n"
         "       counterexample\n");

  struct instance *curr;
  for (int i = 0; (curr = (r->ce + i))->name; i++) {
    printf("         %s = ", curr->name);
    variant_print(curr->v);
    printf("\n");
  }
}

void result_summary_print(int ac[2], bool color) {
  int passed = ac[true];
  int failed = ac[false];

  char *reset = color ? "\e[0m" : "";
  char *bold = color ? "\e[1m" : "";

  printf("\n"
         "  %s**** summary%s\n"
         "       passed: %d\n"
         "       failed: %d\n"
         "       total:  %d\n",
         bold, reset, passed, failed, passed + failed);
}

/*
 * Primitive "dynamic" array to construct instance array.
 */
void _array_grow(void **A, int n, size_t t) { *A = realloc(*A, n * t); }
#define array_grow(A, n) _array_grow((void **)&(A), (n) + 1, sizeof((A)[0]))
#define array_push(A, n, e) (array_grow((A), (n)), (A)[(n)] = (e))

/*
 * Verify a property
 */
struct report run(struct property *prop) {

  struct instance *instances = NULL;
  struct input_space *curr = NULL;
  int n = 0;

  // Construct a lookup table for arguments and their instances.
  for (n = 0; (curr = prop->space + n)->name; n++) {
    array_push(instances, n, instance_init(curr->name, (struct variant){0}));
  }
  array_push(instances, n, (struct instance){0});

  struct report r = (struct report){0};

  // Evaluate the predicate the amount of times configured, recording the
  // iteration in the report.
  int iter = prop->iter ? prop->iter : ITER_DEFAULT;
  for (int i = 0; i < iter; i++) {
    void *ctx = prop->prestate ? prop->prestate() : NULL;

    // Populate lookup table using output from generator functions.
    for (int i = 0; (curr = prop->space + i)->name; i++) {
      instances[i].v = curr->gen ? curr->gen() : (struct variant){0};
    }

    double elapsed = timer();

    r.ok = prop->predicate(instances, ctx);

    // Add result to accumulator.
    r.ac[r.ok]++;

    // Record counterexample if this is our first failure.
    if (!r.ok && r.ac[r.ok] == 1) {
      r.n = n + 1; // We copy n + 1 elements in order to include the sentinel
                   // value we've already constructed.
      size_t size = sizeof(struct instance) * r.n;
      r.ce = malloc(size);
      memcpy(r.ce, instances, size);
    }

    elapsed = timer() - elapsed;
    r.t.mean = mean(r.t.mean, elapsed, i);
    minmax(&r.t.min, &r.t.max, elapsed);

    if (prop->poststate)
      prop->poststate(ctx);
  }

  free(instances);

  return r;
}

/*
 * Verify each property in a specification and collect analytics.
 */
int run_spec(struct specification spec) {
  int ac[2] = {0};
  bool color = isatty(STDOUT_FILENO);

  printf("Verifying '%s' specification\n", spec.name);
  for (int i = 0; (spec.props + i)->name; i++) {

    int pipefd[2];
    if (pipe(pipefd))
      exit(EXIT_FAILURE);

    pid_t cpid = fork();

    if (cpid == 0) {
      close(pipefd[0]);

      struct report r = run(spec.props + i);

      if (write(pipefd[1], &r, sizeof(struct report)) < 0)
        exit(EXIT_FAILURE);

      if (r.n) {
        if (write(pipefd[1], r.ce, sizeof(struct instance) * r.n) < 0)
          exit(EXIT_FAILURE);
      }

      close(pipefd[1]);

      if (r.ce)
        free(r.ce);

      exit(EXIT_SUCCESS);
    }

    if (cpid == -1) {
      close(pipefd[0]);
      close(pipefd[1]);
      exit(EXIT_FAILURE);
    }

    close(pipefd[1]);

    struct report r = {0};
    if (read(pipefd[0], &r, sizeof(struct report)) < 0)
      exit(EXIT_FAILURE);

    // Read if we have counterexamples waiting for us in the pipe.
    if (r.n) {
      r.ce = malloc(sizeof(struct instance) * r.n);
      if (read(pipefd[0], r.ce, sizeof(struct instance) * r.n) < 0)
        exit(EXIT_FAILURE);
    }

    close(pipefd[0]);

    int status;
    waitpid(cpid, &status, 0);

    if (WIFSIGNALED(status)) {
      r.ok = false;
      r.ac[r.ok]++;
    }

    r.ok = r.ac[false] == 0;
    result_print(spec.props + i, &r, color);

    ac[r.ok]++;

    if (r.ce)
      free(r.ce);
  }

  result_summary_print(ac, color);
  return 0;
};
