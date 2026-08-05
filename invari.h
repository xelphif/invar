#ifndef INVARI_H
#define INVARI_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Convenience macros.
 */
#define PREDICATE(p) bool p(struct instance *table, void *ctx)

#define SPACE (struct input_space[])

#define SAMPLE(name, expr)                                                     \
  struct variant name(struct gen *self, unsigned int *seed, int size) {        \
    return wrap(expr);                                                         \
  }

#define GEN(g) .T = SPACE_GENERATOR, .gen = &(g)
#define FIXED_I(v) .T = SPACE_FIXED, .fixed = wrap_i(v)
#define FIXED_D(v) .T = SPACE_FIXED, .fixed = wrap_d(v)
#define FIXED_STR(v) .T = SPACE_FIXED, .fixed = wrap_str(v)
#define FIXED_PTR(v) .T = SPACE_FIXED, .fixed = wrap_ptr(v)

#define ELEM (struct gen *[])

#define TYPES (size_t[])

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
void *apush(struct arena *a, void *src, size_t size);

/*
 * generators: test data generation.
 */
struct gen {
  struct variant (*sample)(struct gen *, unsigned int *, int);
  int size;
  int p;
  struct gen **elem;
  size_t *types;
};

void split(int size, int *l, int *r);
void splitr(int size, unsigned int *seed, int *l, int *r);
bool bernoulli(int p, unsigned int *seed);

/*
 * input space: container for all input generators accessible to a predicate.
 */
enum space_type {
  SPACE_GENERATOR,
  SPACE_FIXED,
};

struct input_space {
  char *name;
  enum space_type T;
  union {
    struct gen *gen;
    struct variant fixed;
  };
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
 * Configuration that can be defined at the property and specification level.
 */
struct config {
  unsigned int seed;
  int iterations;
  size_t alloc;
};

/*
 * property: system under test.
 */
struct property {
  char *name;
  bool (*predicate)(struct instance *table, void *ctx);
  void *(*prestate)(void);
  void (*poststate)(void *ctx);
  struct input_space *space;
  struct config cfg;
};

/*
 * specification: full definition of the properties of an object.
 */
struct specification {
  char *name;
  struct property *props;
  struct config cfg;
};

int run_spec(struct specification *spec);

#endif
