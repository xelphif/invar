#ifndef INVARI_H
#define INVARI_H

#include <assert.h>

/*
 * Generic container for function parameters.
 */
enum variant_type {
  VARIANT_NONE,
  VARIANT_INT,
  VARIANT_FLOAT,
  VARIANT_STR,
  VARIANT_PTR
};

struct variant {
  enum variant_type T;
  union {
    int i;
    float f;
    char *str;
    void *ptr;
  };
};

/*
 * Type specific wrap functions for the generic wrapper macro.
 */
struct variant _wrap_int(int i);
struct variant _wrap_float(float f);
struct variant _wrap_str(char *);
struct variant _wrap_ptr(void *);

#define wrap(x)                                                                \
  _Generic((x),                                                                \
             int: _wrap_int,                                                   \
             float: _wrap_float,                                               \
             char*: _wrap_str,                                                 \
             void*: _wrap_ptr)(x)

/*
 * Explicit wrap macros for static declaration.
 */
#define wrap_i(x) ((struct variant){.T = VARIANT_INT, .i = (x)})
#define wrap_f(x) ((struct variant){.T = VARIANT_FLOAT, .f = (x)})
#define wrap_str(x) ((struct variant){.T = VARIANT_STR, .str = (x)})
#define wrap_ptr(x) ((struct variant){.T = VARIANT_PTR, .ptr = (x)})

/*
 * MAYBE KEEP: 'None' type for end of sequences.
 */
#define wrap_none() ((struct variant){.T = VARIANT_NONE, .i = 0})

#define unwrap_as_int(v) (assert((v).T == VARIANT_INT), (v).i)
#define unwrap_as_float(v) (assert((v).T == VARIANT_FLOAT), (v).f)
#define unwrap_as_str(v) (assert((v).T == VARIANT_STR), (v).str)
#define unwrap_as_ptr(v) (assert((v).T == VARIANT_PTR), (v).ptr)

/*
 * Container for all possible values a test paramter can take on.
 *
 * TODO: implement sequences and wild card inputs.
 */
struct input_space {
  char *name;
  struct variant *v;
};

struct instance {
  char *name;
  struct variant v;
};

struct operation {
  char *name;
  void (*fn)(struct instance *p);
  struct input_space *space;
};

enum result_type {
  RESULT_OK,
  RESULT_FAIL,
  RESULT_SKIP,
};

struct result {
  enum result_type rt;
  double t_elapsed;
};

// TEMP DECL to test ergo
int run_args(struct operation t);

#endif
