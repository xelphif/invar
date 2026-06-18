/*
 * invari.c - simple testing framework
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "invari.h"

#define ITER_DEFAULT 100

#define RESET "\e[0m"
#define BOLD "\e[1m"
#define RED "\e[1;31m"
#define GREEN "\e[1;32m"

/*
 * Error reporting.
 */
noreturn void die(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  fprintf(stderr, RED "[ERR]" RESET " ");
  vfprintf(stderr, fmt, args);
  fputc('\n', stderr);

  va_end(args);
  abort();
}

/*
 * variant: internal helper functions to wrap variants generically.
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

void variant_copy(struct arena *a, struct variant *v) {
  switch (v->T) {
    // We don't copy integers or doubles since they are part of the struct.
  case VARIANT_INT:
  case VARIANT_DOUBLE:
  case VARIANT_NONE:
    return;

    // Default behavior for strings is to deep copy if it exists.
  case VARIANT_STR:

    if (!v->str)
      return;

    size_t n = strlen(v->str) + 1;
    v->str = apush(a, v->str, n);

    return;

  default:
    return;
  }
}

/* Default printing behavior for each variant type. */
void variant_print(struct variant *v) {
  switch (v->T) {
  case VARIANT_INT:
    printf("%d", v->i);
    break;

  case VARIANT_DOUBLE:
    printf("%f", v->d);
    break;

  case VARIANT_STR:
    printf("\"%s\"", v->str);
    break;

  case VARIANT_PTR:
    printf("<ptr %p>", v->ptr);
    break;

  default:
    printf("<none>");
  }
}

/*
 * utilities
 */

/* Get current time with nanosecond precision. */
static inline double timer(void) {
  struct timespec ts;
  clockid_t id = CLOCK_MONOTONIC_RAW;

  clock_gettime(id, &ts);

  return (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
}

/* Timing statistics helper functions. */
static inline double mean(double old, double t, int n) {
  return ((old * n) + t) / (n + 1);
}
static inline double max(double x, double y) { return x > y ? x : y; }

/* FNV-1a hashing algorithm. Further reading:
 * https://en.wikipedia.org/wiki/Fowler-Noll-Vo_hash_function */
static inline unsigned int hash(char *str) {
  unsigned long hash = 0xcbf29ce484222325; // FNV offset basis
  unsigned long prime = 0x100000001b3;     // FNV prime

  for (char *c = str; *c != '\0'; c++) {
    hash ^= *c;
    hash *= prime;
  }

  return (unsigned int)((hash >> 32) ^ hash);
}

/* Get a random number from /dev/urandom. Fallback to unix time if the getrandom
 * call fails. */
static inline unsigned int nrand() {
  unsigned int r = 0;
  if (getrandom((void *)&r, sizeof(r), 0) < 0)
    r = time(NULL);

  return r;
}

/*
 * generators
 */

/* Calculate the maxium amount of bytes an arbitrary composition of sized
 * generators can allocate in order to properly size our shared memory. */
size_t gen_max_bytes_inner(struct gen *gen, size_t *defer) {
  size_t bytes = 0;
  size_t max_child = 0;

  // In the event of child nodes that do not have bounds defined, they are
  // assumed to share their bound with their parent or closest bounded ancestor.
  // This means that our estimate is the size of the largest type multiplied by
  // the bound of the ancestor, much like how a union behaves.
  for (struct gen **e = gen->elem; e && *e; e++) {
    size_t defer_child = 0;
    bytes += gen_max_bytes_inner(*e, &defer_child);
    max_child = max(max_child, defer_child);
  }

  size_t max_type = 0;
  for (size_t *t = gen->types; t && *t != -1; t++)
    max_type = max(max_type, *t);

  max_type = max(max_type, max_child);

  if (gen->size == 0) {
    *defer = max_type;
    return bytes;
  }

  return (bytes + max_type) * gen->size;
}

size_t gen_max_bytes(struct gen *gen) {
  size_t defer = 0;
  size_t bytes = gen_max_bytes_inner(gen, &defer);

  // If defer is not reset, that means we have a generator that must allocate
  // (i.e. a generator not of the types int, float, or none) that is either at
  // the root and unbounded or has no ancestor with a defined bound.
  if (defer)
    die("generator allocates type of size %zu and is unbounded, set the bound "
        "of this generator or one of its ancestors",
        defer);

  return bytes;
}

/* Split a size into two even parts. */
void split(int size, int *l, int *r) {
  *l = size / 2;
  *r = size - *l;
}

/* Split a size into two random parts. */
void splitr(int size, unsigned int *seed, int *l, int *r) {
  *l = size ? rand_r(seed) % (size + 1) : 0;
  *r = size - *l;
}

/* Bernoulli random variable generator; weighted coin flip. It is inverted from
 * the typical implementation, thus p represents the probability of an event NOT
 * happening. */
bool bernoulli(int p, unsigned int *seed) {
  return (rand_r(seed) % 100) + 1 > p;
}

/*
 * instance
 */

/* Initialize our variant lookup table. */
struct instance *instance_table_init(struct input_space *space) {
  int n = 0;
  for (struct input_space *e = space; e->name; e++)
    n++;

  size_t size = sizeof(struct instance) * (n + 1);
  struct instance *A = malloc(size);

  if (!A)
    die("failed to allocate %zu bytes", size);

  for (int i = 0; i < n; i++)
    A[i] = (struct instance){.space = (space + i)};
  A[n] = (struct instance){0};

  return A;
}

/* Free an instance if its input space has a free callback set. */
void instance_free(struct instance *A) {
  for (struct instance *e = A; e->space; e++)
    if (e->space->free)
      e->space->free(&e->v);
}

/* Populate instance table with values generated by input_space */
void instance_table_gen(struct instance *A, unsigned int *seed) {
  for (struct instance *e = A; e->space; e++) {
    struct gen *gen = &e->space->gen;

    switch (e->space->T) {
    case SPACE_GENERATOR:
      e->v =
          gen->sample ? gen->sample(gen, seed, gen->size) : (struct variant){0};
      break;
    case SPACE_FIXED:
      e->v = e->space->fixed;
      break;
    }
  }
}

/* Search for a variant by instance name. */
struct variant instance_get(struct instance *A, char *k) {
  for (struct instance *e = A; e->space; e++) {
    if (!strcmp(e->space->name, k))
      return e->v;
  }

  return (struct variant){0};
}

/* Copy a variant using the specified callback. If no call back is set, use
 * default behavior. */
void instance_copy(struct arena *a, struct instance *inst) {
  if (inst->space->T == SPACE_FIXED)
    return;

  if (inst->space->copy)
    inst->space->copy(a, &inst->v);
  else
    variant_copy(a, &inst->v);
}

/* Print a variant using the specified callback. If no callback is set, use the
 * default behavior. */
void instance_print(struct instance *inst) {
  if (inst->space->print)
    inst->space->print(&inst->v);
  else
    variant_print(&inst->v);
}

size_t instance_max_bytes(struct input_space *space) {
  size_t bytes = 0;

  for (struct input_space *e = space; e->name; e++) {
    bytes += sizeof(struct instance);

    if (e->T == SPACE_FIXED)
      continue;

    bytes += gen_max_bytes(&e->gen);
  }
  bytes += sizeof(struct instance);

  return bytes;
}

/*
 * Bump allocate in arena.
 */
void *aalloc(struct arena *a, size_t size) {
  if (a->off + size > a->cap)
    return NULL;

  void *ptr = a->base + a->off;
  a->off += size;

  return ptr;
}

void *apush(struct arena *a, void *src, size_t size) {
  void *ptr = aalloc(a, size);

  if (!ptr)
    die("failed to allocate %zu bytes in shared arena", size);

  memcpy(ptr, src, size);

  return ptr;
}

/*
 * report: information to output upon test completion.
 */
struct report {
  bool ok;
  struct {
    double mean;
    double max;
  } t;
  int ac[2];
  int signal;
  struct {
    struct instance *i;
    int it;
  } ce;
};

/* Collect test metrics in report. */
void report_record_result(struct report *r, bool ok, double elapsed, int n) {
  r->ok &= ok;
  r->ac[ok]++;
  r->t.mean = mean(r->t.mean, elapsed, n);
  r->t.max = max(r->t.max, elapsed);
}

/* Copy failed instances to shared memory arena for counterexample reporting in
 * output. */
void report_record_ce(struct arena *a, struct report *r, struct instance *A,
                      int it) {
  r->ce.it = it;

  int n = 0;
  for (struct instance *e = A; e->space; e++)
    n++;

  size_t size = sizeof(struct instance) * (n + 1);
  r->ce.i = apush(a, A, size);

  for (int i = 0; i < n; i++) {
    instance_copy(a, r->ce.i + i);
  }
}

/* Process output from waitpid. We consider a crashed process a failure. */
void report_process_status(struct report *r, int status) {

  if (!WIFSIGNALED(status))
    return;

  r->ok = false;
  r->ac[false]++;
  r->signal = WTERMSIG(status);
}

static char *result_str[] = {
    [false] = "FAIL",
    [true] = "OK",
};

/*
 * Output
 */
void result_print_header(struct property *prop, struct report *r) {
  int passed = r->ac[true];
  int failed = r->ac[false];
  int total = passed + failed;

  printf("\n"
         "  %s%-4s%s %s%s%s\n"
         "       %d out of %d passed\n"
         "       seed:    0x%08x\n",
         r->ok ? GREEN : RED, result_str[r->ok], RESET, BOLD, prop->name, RESET,
         passed, total, prop->cfg.seed);
}

void result_print_timing(struct report *r) {
  printf("       mean:    %.9fs\n"
         "       worst:   %.9fs\n",
         r->t.mean, r->t.max);
}

#define SIGNAL RED "!" RESET
void result_print_signal(struct report *r) {
  printf("     " SIGNAL " signal:  %s\n", strsignal(r->signal));
}

void result_print_ce(struct report *r) {
  printf("\n"
         "       counterexample\n");

  for (struct instance *e = r->ce.i; e->space; e++) {
    printf("         %s = ", e->space->name);
    instance_print(e);
    printf("\n");
  }
}

void result_print(struct property *prop, struct report *r) {

  result_print_header(prop, r);

  if (!r->signal)
    result_print_timing(r);
  else
    result_print_signal(r);

  if (r->ce.i)
    result_print_ce(r);
}

void result_summary_print(int ac[2]) {
  int passed = ac[true];
  int failed = ac[false];

  printf("\n"
         "  %s**** summary%s\n"
         "       passed: %d\n"
         "       failed: %d\n"
         "       total:  %d\n",
         BOLD, RESET, passed, failed, passed + failed);
}

/*
 * Shared memory
 */
struct shared {
  struct report r;
  struct arena a;
};

/*
 * Verify a property.
 */
void run(struct property *prop, struct shared *shm) {

  struct instance *table = NULL;

  struct report *r = &shm->r;
  struct arena *a = &shm->a;

  unsigned int seed = prop->cfg.seed;

  // Construct a lookup table for arguments and their instances.
  if (prop->space)
    table = instance_table_init(prop->space);

  *r = (struct report){.ok = true};

  // Evaluate the predicate the amount of times configured, recording the
  // iteration in the report.
  bool ok = false;
  int iter = prop->cfg.iterations ? prop->cfg.iterations : ITER_DEFAULT;
  for (int i = 0; i < iter; i++) {
    void *ctx = prop->prestate ? prop->prestate() : NULL;

    // Populate lookup table using output from generator functions.
    if (prop->space) {
      instance_table_gen(table, &seed);
    }
    double elapsed = timer();

    ok = prop->predicate(table, ctx);

    elapsed = timer() - elapsed;

    // Record counterexample if this is our first failure.
    if (prop->space && !ok && !r->ce.i) {
      report_record_ce(a, r, table, i);
    }

    report_record_result(r, ok, elapsed, i);

    instance_free(table);

    if (prop->poststate)
      prop->poststate(ctx);
  }

  if (table)
    free(table);
}

/*
 * Execute property test in separate process.
 */
void run_isolated(struct property *prop, struct shared *shm) {

  pid_t cpid = fork();

  if (cpid == 0) {
    run(prop, shm);

    exit(EXIT_SUCCESS);
  }

  if (cpid == -1) {
    die("failed to fork process");
  }

  int status;
  waitpid(cpid, &status, 0);

  report_process_status(&shm->r, status);
}

/*
 * Verify each property in a specification and collect analytics.
 */
int run_spec(struct specification spec) {
  int ac[2] = {0};

  // If no seed is set, generate a seed using a hash of the specification name
  // and bytes from /dev/urandom. If getrandom() fails, we use the current
  // time instead.
  if (!spec.cfg.seed)
    spec.cfg.seed = nrand() ^ hash(spec.name);

  printf("Verifying '%s' specification with seed 0x%08x\n", spec.name,
         spec.cfg.seed);

  for (struct property *prop = spec.props; prop->name; prop++) {
    // Derive per property seed based on spec seed. Respect explicitly set
    // seeds.
    if (!prop->cfg.seed)
      prop->cfg.seed = spec.cfg.seed ^ hash(prop->name);

    // If our propery doesn't have specified iterations but our specification
    // does, use specification value over default value.
    if (!prop->cfg.iterations && spec.cfg.iterations)
      prop->cfg.iterations = spec.cfg.iterations;

    size_t size = instance_max_bytes(prop->space) + sizeof(struct shared);

    struct shared *shm = mmap(NULL, size, PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (shm == MAP_FAILED)
      die("failed to map shared memory region");

    shm->a.cap = size - sizeof(struct shared);
    struct report *r = &shm->r;

    run_isolated(prop, shm);

    result_print(prop, r);

    ac[r->ok]++;

    if (munmap(shm, size))
      die("failed to unmap shared memory region");
  }

  result_summary_print(ac);
  return 0;
};
