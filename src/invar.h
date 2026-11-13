#ifndef INVAR_H
#define INVAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INVAR_CONFIG_FIELDS(X) \
  X(uint64_t, seed)            \
  X(int, iterations)           \
  X(size_t, capacity)          \
  X(unsigned int, timeout)

struct config {
#define X(type, name) type name;
  INVAR_CONFIG_FIELDS(X)
#undef X
};

/*
 * tape: choice sequence
 */
struct tape;

/*
 * scope: deferrals for generator cleanup.
 */
struct scope;

/* Push callback to LIFO stack to free a generated structure after a run.
 * NULL for the fn argument defaults to free(ptr) */
void defer(struct scope *s, void (*fn)(void *), void *ptr);

/*
 * property: a predicate that must hold true for all generated inputs.
 */
typedef bool (*predicate_fn)(struct tape *t, struct scope *s);

struct property {
  char         *name;
  predicate_fn  predicate;
  struct config cfg;
};

/* Define a property. The body receives `t` (randomness abstraction) and
 * `s` (the scope for defer). Return true if the property holds. */
#define PROPERTY(name) \
  bool name(struct tape *t [[maybe_unused]], struct scope *s [[maybe_unused]])

/* Define a property in the property table. Function name is stringized, and
 * config parameter can be passed as variable args. */
#define PROP(fn, ...)                                    \
  {                                                      \
    .name = #fn, .predicate = fn, .cfg = { __VA_ARGS__ } \
  }

/* Define a generator function. The body receives `t` (randomness abstraction)
 * and `s` (scope for free deferrals). Optionally, extra arguments can be
 * defined in variable args. */
#define GENERATOR(type, name, ...) \
  type name(struct tape *t, struct scope *s __VA_OPT__(, ) __VA_ARGS__)

/*
 * specification: full definition of the properties of an object.
 */
struct specification {
  char *name;

  struct config    cfg;
  struct property *props;
};

/* Uniform draws over the range of each type. */
uint8_t  draw_u8(struct tape *t);
uint16_t draw_u16(struct tape *t);
uint32_t draw_u32(struct tape *t);
uint64_t draw_u64(struct tape *t);
bool     draw_bool(struct tape *t);

/* Uniform draw in [min, max]. */
int64_t draw_range(struct tape *t, int64_t min, int64_t max);

/* Run every property in a specification and report results. Returns 0 if all
 * properties pass, and 1 if any property failed or gave up. */
int verify(const struct specification *spec);

#endif
