#ifndef INVARI_H
#define INVARI_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Convenience macros.
 */
#define PREDICATE(p) bool p(struct instance *i, void *ctx)

#define SPACE (struct input_space[])

#define GEN(name, expr)                                                        \
  struct variant name(struct gen *ctx, int size) {                             \
    return wrap(expr);                                                         \
  }

/*
 * variant: generic container for function parameters.
 */
enum variant_type {
  VARIANT_NONE,
  VARIANT_INT,
  VARIANT_DOUBLE,
  VARIANT_STR,
  VARIANT_PTR,
};

struct variant {
  enum variant_type T;
  union {
    int i;
    double d;
    char *str;
    void *ptr;
  };
};

struct variant _wrap_int(int i);
struct variant _wrap_double(double d);
struct variant _wrap_str(char *);
struct variant _wrap_ptr(void *);

#define wrap(x)                                                                \
  _Generic((x),                                                                \
             int: _wrap_int,                                                   \
             double: _wrap_double,                                             \
             char*: _wrap_str,                                                 \
             void*: _wrap_ptr)(x)

#define wrap_i(x) ((struct variant){.T = VARIANT_INT, .i = (x)})
#define wrap_d(x) ((struct variant){.T = VARIANT_DOUBLE, .d = (x)})
#define wrap_str(x) ((struct variant){.T = VARIANT_STR, .str = (x)})
#define wrap_ptr(x) ((struct variant){.T = VARIANT_PTR, .ptr = (x)})

#define unwrap_as_int(v) (assert((v).T == VARIANT_INT), (v).i)
#define unwrap_as_double(v) (assert((v).T == VARIANT_DOUBLE), (v).d)
#define unwrap_as_str(v) (assert((v).T == VARIANT_STR), (v).str)
#define unwrap_as_ptr(v) (assert((v).T == VARIANT_PTR), (v).ptr)

struct arena {
  size_t off;
  size_t cap;
  unsigned char base[];
};

void *aalloc(struct arena *a, size_t size);

/*
 * generators: test data generation.
 */
struct gen {
  unsigned int *seed;
  struct arena *a;
  int p;
};

/*
 * input space: container for all input generators accessible to a predicate.
 */
struct input_space {
  char *name;
  struct variant (*gen)(struct gen *, int);
  int (*copy)(struct arena *a, struct variant *v);
  void (*free)(struct variant *v);
  void (*print)(struct variant *v);
};

/*
 * instance: entry in input lookup table provided to predicate.
 */
struct instance {
  struct input_space *space;
  struct variant v;
};

struct variant instance_get(struct instance *A, char *k);

#define given_int(i, k) (unwrap_as_int(instance_get((i), (k))))
#define given_double(i, k) (unwrap_as_double(instance_get((i), (k))))
#define given_str(i, k) (unwrap_as_str(instance_get((i), (k))))
#define given_ptr(i, k) (unwrap_as_ptr(instance_get((i), (k))))

/*
 * property: system under test.
 */
struct property {
  char *name;
  bool (*predicate)(struct instance *i, void *ctx);
  void *(*prestate)(void);
  void (*poststate)(void *ctx);
  struct input_space *space;
  unsigned int seed;
  int iterations;
};

/*
 * specification: full definition of the properties of an object.
 */
struct specification {
  char *name;
  struct property *props;
  unsigned int seed;
};

int run_spec(struct specification spec);

#endif
