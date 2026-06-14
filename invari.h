#ifndef INVARI_H
#define INVARI_H

#include <assert.h>
#include <stdbool.h>

/*
 * Generic container for function parameters.
 */
enum variant_type {
  VARIANT_NONE,
  VARIANT_INT,
  VARIANT_DOUBLE,
  VARIANT_STR,
  VARIANT_PTR
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

/*
 * Type specific wrap functions for the generic wrapper macro.
 */
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

/*
 * Explicit wrap macros for static declaration.
 */
#define wrap_i(x) ((struct variant){.T = VARIANT_INT, .i = (x)})
#define wrap_d(x) ((struct variant){.T = VARIANT_DOUBLE, .d = (x)})
#define wrap_str(x) ((struct variant){.T = VARIANT_STR, .str = (x)})
#define wrap_ptr(x) ((struct variant){.T = VARIANT_PTR, .ptr = (x)})

#define unwrap_as_int(v) (assert((v).T == VARIANT_INT), (v).i)
#define unwrap_as_double(v) (assert((v).T == VARIANT_DOUBLE), (v).d)
#define unwrap_as_str(v) (assert((v).T == VARIANT_STR), (v).str)
#define unwrap_as_ptr(v) (assert((v).T == VARIANT_PTR), (v).ptr)

/*
 * Container for all possible values a test paramter can take on.
 *
 * TODO: implement sequences and wild card inputs.
 */
struct input_space {
  char *name;
  struct variant (*gen)(void);
};

struct instance {
  char *name;
  struct variant v;
};

struct variant instance_get(struct instance *A, char *k);

struct report {
  bool ok;
  struct {
    double mean;
    double min;
    double max;
  } t;
  int ac[2];
  struct instance *ce;
  int n;
};

struct property {
  char *name;
  bool (*predicate)(struct instance *i, void *ctx);
  void *(*prestate)(void);
  void (*poststate)(void *ctx);
  struct input_space *space;
  int iter;
};

struct specification {
  char *name;
  struct property *props;
};

// TEMP DECL to test ergo
int run_spec(struct specification spec);

#endif
