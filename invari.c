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
    [RESULT_OK] = " OK ",
    [RESULT_FAIL] = "FAIL",
    [RESULT_SKIP] = "SKIP",
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

  void *ctx = prop->prestate ? prop->prestate() : NULL;

  struct report r = (struct report){0};

  // Execute function n times, recording the iteration in the report.
  for (r.it = 0; r.it < 100; r.it++) {

    // Populate lookup table using output from generator functions.
    for (int i = 0; (curr = prop->space + i)->name; i++) {
      instances[i].v = curr->gen ? curr->gen() : (struct variant){0};
    }

    double elapsed = timer();

    prop->predicate(instances, ctx);

    r.elapsed = timer() - elapsed;
  }

  if (prop->poststate)
    prop->poststate(ctx);

  free(instances);

  return r;
}

/*
 * Verify each property in a specification and collect analytics.
 */
int run_spec(struct specification spec) {

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

      close(pipefd[1]);

      exit(EXIT_SUCCESS);
    }

    if (cpid == -1) {
      close(pipefd[0]);
      close(pipefd[1]);
      exit(EXIT_FAILURE);
    }

    close(pipefd[1]);

    struct report r;
    if (read(pipefd[0], &r, sizeof(struct report)) == -1)
      exit(EXIT_FAILURE);

    close(pipefd[0]);

    int status;
    waitpid(cpid, &status, 0);

    if (WIFEXITED(status)) {
      r.r = WEXITSTATUS(status) ? RESULT_FAIL : RESULT_OK;
    }

    if (WIFSIGNALED(status)) {
      r.r = RESULT_FAIL;
    }

    printf("  [%s] %s... %.9fs\n", result_str[r.r], (spec.props + i)->name,
           r.r ? 0 : r.elapsed);
  }

  return 0;
};
