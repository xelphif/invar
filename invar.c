/*
 * invar.c
 *
 * Property based testing harness heavily inspired by Hypothesis. The
 * framework features rudimentary process isolation which allows us to test
 * freely without bringing the entire harness down. Seeding is derived
 * deterministically using a SplitMix64 implementation. Randomness sourced from
 * the mixing function is reduced and stored in shared memory in order to
 * preserve a run in the event of a crash. This allows us to deterministically
 * reproduce any run from the seed and iteration of a trial.
 */

#include <inttypes.h>
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

#include "invar.h"

#define TAPE_DEFAULT_CAP  8192
#define PROP_DEFAULT_ITER 100

#define RESET  "\033[0m"
#define BOLD   "\033[1m"
#define RED    "\033[1;31m"
#define GREEN  "\033[1;32m"
#define YELLOW "\033[1;33m"

/*
 * Error reporting.
 */
static noreturn void die(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  fprintf(stderr, RED "[ERR]" RESET " ");
  vfprintf(stderr, fmt, args);
  fputc('\n', stderr);

  va_end(args);
  abort();
}

/*
 * Output
 */
struct status {
  bool signaled;
  int  code;
};

struct result {
  bool finished;

  /* Exit statuses for trials to be stored in a result. FAIL and OK must stay at
   * their indexes of 0 and 1 respectively in order to preserve boolean
   * indexing.
   */
  enum verdict { FAIL, OK, SKIP } verdict;

  int ac[3];
  int iter;

  struct status status;
};

struct result_str {
  const char *str;
  const char *color;
};

static struct result_str result_str[] = {
    [FAIL] = {"FAIL", RED},
    [OK]   = {"OK", GREEN},
    [SKIP] = {"SKIP", YELLOW},
};

static inline void result_print_header(struct property *prop,
                                       struct result   *r) {
  int passed  = r->ac[OK];
  int failed  = r->ac[FAIL];
  int skipped = r->ac[SKIP];
  int total   = passed + failed;

  printf("\n"
         "  %s%-4s%s %s%s%s\n"
         "       %d out of %d passed, %d skipped\n"
         "       seed:    0x%016" PRIx64,
         result_str[r->verdict].color, result_str[r->verdict].str, RESET, BOLD,
         prop->name, RESET, passed, total, skipped, prop->cfg.seed);

  if (r->ac[FAIL]) printf(", iteration %d", r->iter);

  printf("\n");
}

#define SIGNAL RED "!" RESET
static inline void result_print_signal(int signal) {
  printf("     " SIGNAL " signal:  %s\n", strsignal(signal));
}

#define EXITED YELLOW "!" RESET
static inline void result_print_exit(int code) {
  printf("     " EXITED " exited:  predicate exited early with code %d\n",
         code);
}

static inline void result_print(struct property *prop, struct result *r) {
  result_print_header(prop, r);

  if (r->status.signaled)
    result_print_signal(r->status.code);
  else if (!r->finished)
    result_print_exit(r->status.code);
}

static inline void result_print_summary(int ac[3]) {
  int passed  = ac[OK];
  int failed  = ac[FAIL];
  int skipped = ac[SKIP];

  printf("\n"
         "  %s**** summary%s\n"
         "       passed:  %d\n"
         "       failed:  %d\n"
         "       skipped: %d\n"
         "       total:   %d\n",
         BOLD, RESET, passed, failed, skipped, passed + failed + skipped);
}

/*
 * PRNG
 */

/* Get a random number from /dev/urandom. Fallback to unix time if the getrandom
 * call fails. */
static inline uint64_t getseed(void) {
  uint64_t r = 0;
  if (getrandom((void *)&r, sizeof(r), 0) < 0) r = time(NULL);

  return r;
}

/* Addressable, stateless PRNG
 * https://rosettacode.org/wiki/Pseudo-random_numbers/Splitmix64# */
static inline uint64_t mix(uint64_t seed, uint64_t index) {
  uint64_t z = seed + (index + 1) * 0x9e3779b97f4a7c15ULL;

  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;

  return z ^ (z >> 31);
}

/* FNV-1a hashing algorithm. Further reading:
 * https://en.wikipedia.org/wiki/Fowler-Noll-Vo_hash_function */
static inline uint64_t hash(const char *str) {
  uint64_t hash  = 0xcbf29ce484222325; // FNV offset basis
  uint64_t prime = 0x100000001b3;      // FNV prime

  for (const unsigned char *c = (const unsigned char *)str; *c != '\0'; c++) {
    hash ^= *c;
    hash *= prime;
  }

  return hash;
}

/*
 * Configuration
 */
static struct config overlay(struct config base, struct config top) {
#define X(type, name) \
  if (!top.name) top.name = base.name;
  INVAR_CONFIG_FIELDS(X)
#undef X
  return top;
}

/*
 * Choice sequence
 */
struct tape {
  size_t   off;     /* read cursor, next word to read/write */
  size_t   len;     /* words recorded, off <= len <= cap */
  size_t   cap;     /* capacity in words */
  bool     overrun; /* tape exceeded flag */
  bool     replay;  /* replay flag */
  uint64_t seed;    /* seed for the current generation run */
  uint64_t w[];
};

// Rewind tape for replays without affecting the length.
static inline void trewind(struct tape *t) {
  t->off     = 0;
  t->overrun = false;
  t->replay  = true;
}

// Seed tape and clear for new trial.
static inline void tseed(struct tape *t, uint64_t seed) {
  t->off     = 0;
  t->len     = 0;
  t->overrun = false;
  t->replay  = false;
  t->seed    = seed;
}

/*
 * Draw a 64 bit word in [0, n] from the choice sequence tape.
 * During generation, we derive the entropy using the word's index in the tape
 * and the seed. During replay, we read the already derived indexes until we
 * reach the length of the tape. We also clamp values based on their respective
 * type, in the event of a word with a larger value than what the type can
 * represent is moved into place. Once we are past the end of the replay, or at
 * the end of the tape during generation, set the overrun flag and return 0.
 */
static inline uint64_t draw_word(struct tape *t, uint64_t n) {
  if (t->off < t->len) {
    if (t->w[t->off] > n) t->w[t->off] = n;
    return t->w[t->off++];
  }

  // Return zero if we reach the end of the tape during generation or replay.
  if (t->replay || t->len == t->cap) {
    t->overrun = true;
    return 0;
  }

  uint64_t raw = mix(t->seed, t->len);
  uint64_t w   = n == UINT64_MAX ? raw : raw % (n + 1);

  // During generation, len == off, so we increment
  // both together.
  return t->w[t->len++, t->off++] = w;
}

uint64_t draw_u64(struct tape *t) { return draw_word(t, UINT64_MAX); }
uint32_t draw_u32(struct tape *t) { return draw_word(t, UINT32_MAX); }
uint16_t draw_u16(struct tape *t) { return draw_word(t, UINT16_MAX); }
uint8_t  draw_u8(struct tape *t) { return draw_word(t, UINT8_MAX); }
bool     draw_bool(struct tape *t) { return draw_word(t, 1); }

int64_t draw_range(struct tape *t, int64_t min, int64_t max) {
  return (int64_t)((uint64_t)min + draw_word(t, (uint64_t)max - (uint64_t)min));
}

/*
 * Shared memory.
 */
struct shared {
  struct result r;
  struct tape   t;
};

/*
 * Memory lifetime management.
 */
struct deferral {
  void (*fn)(void *);
  void            *ptr;
  struct deferral *next;
};

struct scope {
  struct deferral *head;
};

void defer(struct scope *s, void (*fn)(void *), void *ptr) {
  struct deferral *d = malloc(sizeof(struct deferral));

  if (!d) die("failed to allocate deferral node");

  d->fn  = fn ? fn : free;
  d->ptr = ptr;

  d->next = s->head;
  s->head = d;
}

static inline void unwind(struct scope s) {
  while (s.head) {
    struct deferral *d = s.head;
    s.head             = d->next;

    d->fn(d->ptr);
    free(d);
  }
}

/*
 * Isolate function in order to prevent predicates from crashing the runners.
 */
typedef enum verdict (*runner_fn)(struct tape *t, struct result *r,
                                  struct property *prop);

static inline struct status isolate(runner_fn fn, struct tape *t,
                                    struct result *r, struct property *prop) {

  fflush(stdout);

  pid_t cpid = fork();

  if (cpid == 0) {
    shm->r.exit = fn(&shm->t, &shm->r, prop);
    exit(EXIT_SUCCESS);
  }

  if (cpid == -1) die("failed to fork process");

  int status;
  waitpid(cpid, &status, 0);

  struct status s = {0};

  s.signaled = WIFSIGNALED(status);
  s.code     = s.signaled ? WTERMSIG(status) : WEXITSTATUS(status);

  return s;
}

static inline enum verdict setverdict(struct result *r, enum verdict v) {
  return (r->finished = true, r->verdict = v);
}

/* Run predicate based on seed and iterations, returning on first failure for
 * potential shrinking. */
static inline enum verdict run(struct tape *t, struct result *r,
                               struct property *prop) {
  for (int i = 0, passed = 0; passed < prop->cfg.iterations; i++) {

    r->iter = i;
    tseed(t, mix(prop->cfg.seed, i));

    struct scope s  = {0};
    bool         ok = prop->predicate(t, &s);
    unwind(s);

    if (t->overrun) {
      if (++r->ac[SKIP] == prop->cfg.iterations) return setverdict(r, SKIP);

      continue;
    }

    r->ac[ok]++;

    if (!ok) return setverdict(r, FAIL);

    passed++;
  }

  return setverdict(r, OK);
}

/*
 * Verify each property in a specification.
 */
int verify(const struct specification *spec) {
  int ac[3] = {0};

  // Calculate effective config based on the following precedence:
  // prop -> spec -> defaults
  const struct config defaults = {
      .seed       = getseed(),
      .iterations = PROP_DEFAULT_ITER,
      .capacity   = TAPE_DEFAULT_CAP,
  };

  struct config c = overlay(defaults, spec->cfg);

  printf("Verifying '%s' specification with seed 0x%016" PRIx64 "\n",
         spec->name, c.seed);

  for (struct property *prop = spec->props; prop->predicate != NULL; prop++) {

    struct property p = *prop;
    p.cfg             = overlay(c, prop->cfg);

    // If the property didn't have a seed, make sure we xor in a hash of the
    // property name in order to make sure each property has a different base
    // seed (and using the property name means we don't have to rely on ordering
    // in the specification file)
    if (!prop->cfg.seed) p.cfg.seed ^= hash(p.name);

    // Map shared memory.
    size_t size =
        offsetof(struct shared, t.w) + p.cfg.capacity * sizeof(uint64_t);
    struct shared *shm = mmap(NULL, size, PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (shm == MAP_FAILED) die("failed to map shared memory region");

    shm->t.cap = p.cfg.capacity;

    if (!r->finished) {
      r->ac[FAIL]++;
      r->verdict = FAIL;
    }

    ac[r->verdict]++;

    result_print(&p, &shm->r);

    if (munmap(shm, size)) die("failed to unmap shared memory region");
  }
  result_print_summary(ac);

  return ac[FAIL] || ac[SKIP];
}
