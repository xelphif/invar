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
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "invar.h"

#define TAPE_DEFAULT_CAP 8192

#define PROP_DEFAULT_ITER    100
#define PROP_DEFAULT_REPLAYS 10

struct deferral {
  void (*fn)(void *);
  void            *ptr;
  struct deferral *next;
};

struct scope {
  struct deferral *head;
};

struct printer {
  bool print;
  int  count;
};

struct context {
  struct tape   *t;
  struct scope   s;
  struct printer pr;
};

/*
 * Error reporting.
 */
[[noreturn]] static void die(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  fprintf(stderr, "[ERR] ");
  vfprintf(stderr, fmt, args);
  fputc('\n', stderr);

  va_end(args);
  abort();
}

/*
 * Output
 */

enum color {
  RESET,
  BOLD,
  RED,
  GREEN,
  YELLOW,
};

static const char *colors[] = {
    [RESET] = "\033[0m",    [BOLD] = "\033[1m",      [RED] = "\033[1;31m",
    [GREEN] = "\033[1;32m", [YELLOW] = "\033[1;33m",
};

static bool        color;
static const char *colorstr(enum color c) { return color ? colors[c] : ""; }

struct result {
  /* Exit statuses for trials to be stored in a result. FAIL and OK must stay at
   * their indexes of 0 and 1 respectively in order to preserve boolean
   * indexing.
   */
  enum verdict { FAIL, OK, SKIP } v;

  bool signaled;
  bool finished;
  int  code;
};

static bool resultcmp(struct result a, struct result b) {
  return (a.finished == b.finished) && (a.signaled == b.signaled) &&
         (a.code == b.code) && (a.v == b.v);
}

struct stats {
  int iter;
  int skipped;
};

struct shared {
  struct result r;
  struct stats  s;
};

struct result_str {
  const char *str;
  enum color  c;
};

static struct result_str result_str[] = {
    [FAIL] = {"FAIL", RED},
    [OK]   = {"OK", GREEN},
    [SKIP] = {"SKIP", YELLOW},
};

static inline void result_print_header(const struct property *prop,
                                       struct config cfg, struct result *r,
                                       struct stats *s, bool flaky) {

  printf("  %s%-4s%s %s%s%s", colorstr(result_str[r->v].c),
         result_str[r->v].str, colorstr(RESET), colorstr(BOLD), prop->name,
         colorstr(RESET));

  if (r->v == OK) {
    printf("\n");
    return;
  }

  if (r->v == SKIP) printf(" -- gave up after %d discards", s->skipped);

  if (flaky) printf(" (%sflaky%s)", colorstr(YELLOW), colorstr(RESET));

  printf("\n");

  if (r->v == SKIP) return;

  printf("       seed:    0x%016" PRIx64, cfg.seed);

  printf(", iteration %d", s->iter);

  printf("\n");
}

static inline void result_print_signal(int signal) {
  printf("     %s!%s signal:  %s\n", colorstr(RED), colorstr(RESET),
         strsignal(signal));
}

static inline void result_print_exit(int code) {
  printf("     %s!%s exited:  predicate exited early with code %d\n",
         colorstr(YELLOW), colorstr(RESET), code);
}

static inline void result_print(const struct property *prop, struct config cfg,
                                struct result *r, struct stats *s, bool flaky) {
  result_print_header(prop, cfg, r, s, flaky);

  if (r->signaled)
    result_print_signal(r->code);
  else if (!r->finished)
    result_print_exit(r->code);
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
         colorstr(BOLD), colorstr(RESET), passed, failed, skipped,
         passed + failed + skipped);
}

/*
 * Display 'note' calls, which are to display counterexamples after shrinking.
 */
[[gnu::format(printf, 2, 3)]]
void note(struct context *ctx, const char *fmt, ...) {
  struct printer *pr = &ctx->pr;

  if (!pr->print) return;

  if (!pr->count++) printf("\n       counterexample:\n");

  va_list args;
  va_start(args, fmt);

  printf("         ");
  vprintf(fmt, args);
  printf("\n");

  va_end(args);

  fflush(stdout);
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

static void tinit(struct tape *t, size_t capacity) { t->cap = capacity; }

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

static size_t tsize(size_t cap) {
  return offsetof(struct tape, w) + cap * sizeof(uint64_t);
}

static void tcopy(struct tape *dest, const struct tape *src) {
  if (dest->cap < src->len) die("attempting to copy a tape larger than dest");

  dest->len = src->len;
  memcpy(dest->w, src->w, sizeof(uint64_t) * src->len);
}
/*
 * Memory lifetime management.
 */

void defer(struct context *ctx, void (*fn)(void *), void *ptr) {
  struct scope *s = &ctx->s;

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
 * Draw a 64 bit word in [0, n] from the choice sequence tape.
 * During generation, we derive the entropy using the word's index in the
 * tape and the seed. During replay, we read the already derived indexes
 * until we reach the length of the tape. We also clamp values based on
 * their respective type, in the event of a word with a larger value than
 * what the type can represent is moved into place. Once we are past the end
 * of the replay, or at the end of the tape during generation, set the
 * overrun flag and return 0.
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

uint64_t draw_u64(struct context *ctx) { return draw_word(ctx->t, UINT64_MAX); }
uint32_t draw_u32(struct context *ctx) { return draw_word(ctx->t, UINT32_MAX); }
uint16_t draw_u16(struct context *ctx) { return draw_word(ctx->t, UINT16_MAX); }
uint8_t  draw_u8(struct context *ctx) { return draw_word(ctx->t, UINT8_MAX); }
bool     draw_bool(struct context *ctx) { return draw_word(ctx->t, 1); }

int64_t draw_range(struct context *ctx, int64_t min, int64_t max) {
  return (int64_t)((uint64_t)min +
                   draw_word(ctx->t, (uint64_t)max - (uint64_t)min));
}

/*
 * Trial data
 */
struct trial {
  struct tape   *t;
  struct result *r;
  struct stats  *s;

  const struct property *prop;
  struct config          cfg;

  struct printer pr;
};

typedef void (*trial_fn)(struct trial *);

/*
 * Isolate function in order to prevent predicates from crashing the runners.
 */
static void isolate(trial_fn fn, struct trial *tr) {

  struct result *r = tr->r;

  fflush(stdout);
  pid_t cpid = fork();

  if (cpid == 0) {
    fn(tr);
    r->finished = true;
    exit(EXIT_SUCCESS);
  }

  if (cpid == -1) die("failed to fork process");

  int status;
  waitpid(cpid, &status, 0);

  r->signaled = WIFSIGNALED(status);
  r->code     = r->signaled ? WTERMSIG(status) : WEXITSTATUS(status);
}

static struct result execute(trial_fn fn, struct trial *tr) {

  struct result *r = tr->r;

  r->finished = false;
  r->v        = FAIL;

  if (tr->cfg.inprocess) {
    fn(tr);
    r->finished = true;
  } else {
    isolate(fn, tr);
  }

  return *r;
}

static bool attempt(struct trial *tr) {
  const struct property *prop = tr->prop;

  struct context ctx = {.t = tr->t, .pr = tr->pr};

  // Set timeout if configured for property.
  if (tr->cfg.timeout) alarm(tr->cfg.timeout);

  bool ok = prop->predicate(&ctx);

  alarm(0);

  unwind(ctx.s);

  return ok;
}

/* Run predicate based on seed and iterations, returning on first failure for
 * potential shrinking. */
static void run(struct trial *tr) {

  struct tape   *t = tr->t;
  struct result *r = tr->r;
  struct stats  *s = tr->s;

  for (int i = 0, passed = 0; passed < tr->cfg.iterations; i++) {

    // Record iteration before execution for crash reporting.
    s->iter = i;
    tseed(t, mix(tr->cfg.seed, i));

    bool ok = attempt(tr);

    // If we discard too many trials, we give up on the property.
    if (t->overrun) {
      if (++s->skipped == tr->cfg.iterations) {
        r->v = SKIP;
        return;
      }

      continue;
    }

    if (!ok) {
      r->v = FAIL;
      return;
    }

    passed++;
  }

  r->v = OK;
}

static void replay(struct trial *tr) {
  struct tape   *t = tr->t;
  struct result *r = tr->r;

  trewind(t);

  bool ok = attempt(tr);

  if (t->overrun)
    r->v = SKIP;
  else
    r->v = ok;
}

// Replay tape and print `note` statements
static void exhibit(struct trial *tr) {
  tr->pr.print = true;
  tr->pr.count = 0;
  execute(replay, tr);
}

// Loads candidate into shared memory and replays. Accept a candidate if the
// outcome matches. If accepted, copy the list in shared memory to our
// interesting buffer, since replay mode clamps all values.
static bool accept(struct trial *tr, struct tape *interesting,
                   struct result outcome) {
  struct tape *t = tr->t;

  tcopy(t, interesting);

  bool accepted;
  if ((accepted = resultcmp(outcome, execute(replay, tr)))) {
    tcopy(interesting, t);
    interesting->len = t->off;
  }

  return accepted;
}

static void reduce(struct trial *tr, struct tape *interesting,
                   struct result outcome) {
  for (size_t i = 0; i < interesting->len; i++) {
    uint64_t hi = interesting->w[i];

    interesting->w[i] = 0;
    if (accept(tr, interesting, outcome)) continue;

    // binary search lowest passing word
    uint64_t lo = 1;
    while (lo < hi) {
      uint64_t mid      = lo + (hi - lo) / 2;
      interesting->w[i] = mid;
      if (accept(tr, interesting, outcome))
        hi = mid;
      else
        lo = mid + 1;
    }

    interesting->w[i] = hi;
  }
}

static bool shrink(struct trial *tr, struct result outcome) {
  struct tape *t = tr->t;

  size_t size = tsize(t->len);

  struct tape *interesting = malloc(size);

  if (!interesting) die("failed to allocate interesting buffer");
  interesting->cap = t->len;
  tcopy(interesting, t);

  bool accepted;
  // If our check fails, that suggests the predicate is not deterministic, thus
  // we omit the reduction and return our result
  if ((accepted = accept(tr, interesting, outcome))) {
    reduce(tr, interesting, outcome);
    tcopy(t, interesting);
  }

  free(interesting);

  return accepted;
}

/* Resolve our effective config for our properties */
static struct config cfgresolve(struct config c, const struct property *p) {

  struct config cfg = overlay(c, p->cfg);

  // If the property didn't have a seed, make sure we xor in a hash of the
  // property name in order to make sure each property has a different base
  // seed (and using the property name means we don't have to rely on ordering
  // in the specification file)
  if (!p->cfg.seed) cfg.seed ^= hash(p->name);

  return cfg;
}

/* Byte offset of the tape in the shared region: the result's size rounded up
 * to the tape's alignment. */
constexpr size_t TAPE_OFFSET =
    (sizeof(struct shared) + alignof(struct tape) - 1) / alignof(struct tape) *
    alignof(struct tape);

static void *shmtape(void *shm) { return ((char *)shm + TAPE_OFFSET); }

static size_t shmsize(size_t capacity) { return TAPE_OFFSET + tsize(capacity); }

/* Initialize shared memory region and align pointers properly */
static void *shminit(size_t capacity) {

  // Map shared memory.
  char *shm = mmap(NULL, shmsize(capacity), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  if (shm == MAP_FAILED) die("failed to map shared memory region");

  return shm;
}

static void shmfree(void *shm, size_t capacity) {
  if (munmap(shm, shmsize(capacity)))
    die("failed to unmap shared memory region");
}

static bool isflaky(struct trial *tr, struct result outcome) {
  struct tape *t = tr->t;

  bool flaky = false;

  for (int i = 0; i < tr->cfg.replays && !flaky; i++) {
    struct result a = execute(replay, tr);

    // A deterministic replay should have the same outcome and the same amount
    // of words consumed.
    flaky = !resultcmp(outcome, a) || t->off != t->len;
  }

  return flaky;
}

/*
 * Check a property and return a verdict.
 */
static enum verdict check(struct config c, const struct property *prop) {
  struct config  cfg = cfgresolve(c, prop);
  struct shared *shm = shminit(cfg.capacity);

  struct trial tr = {
      .prop = prop,
      .r    = &shm->r,
      .s    = &shm->s,
      .t    = shmtape(shm),

      .cfg = cfg,
  };

  tinit(tr.t, cfg.capacity);

  struct result outcome = execute(run, &tr);

  bool flaky = false;
  if (outcome.v == FAIL) {
    flaky = isflaky(&tr, outcome);

    // If we don't have replays, we don't shrink since we cannot verify if the
    // predicate is flaky.
    if (!flaky && cfg.replays > 0) flaky = !shrink(&tr, outcome);
  }

  result_print(prop, cfg, &outcome, tr.s, flaky);

  if (outcome.v == FAIL) exhibit(&tr);

  shmfree(shm, cfg.capacity);

  return outcome.v;
}

/*
 * Verify a specification.
 */
int verify(const struct specification *spec) {

  const char *env = getenv("NO_COLOR");
  if (env && env[0] != '\0')
    color = false;
  else
    color = isatty(STDOUT_FILENO);

  int ac[3] = {0};

  // Calculate effective config based on the following precedence:
  // prop -> spec -> defaults
  const struct config defaults = {
      .seed       = getseed(),
      .iterations = PROP_DEFAULT_ITER,
      .replays    = PROP_DEFAULT_REPLAYS,
      .capacity   = TAPE_DEFAULT_CAP,
  };

  struct config c = overlay(defaults, spec->cfg);

  printf("Verifying '%s' specification with seed 0x%016" PRIx64 "\n",
         spec->name, c.seed);

  for (const struct property *prop = spec->props; prop->predicate != NULL;
       prop++)
    ac[check(c, prop)]++;

  result_print_summary(ac);

  return ac[FAIL] || ac[SKIP];
}
