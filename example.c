#include <stdio.h>
#include <stdlib.h>

#include "invari.h"

struct variant gen_int(void) {
  return wrap(rand() % 100);
}

struct input_space add_spaces[] = {
    {.name = "a", .gen = gen_int},
    {.name = "b", .gen = gen_int},
    {0},
};

/*
 * Data can be threaded through the testing environment using custom structs in
 * tandem with the (pre/post)state functions.
 */
struct add_ctx {
  int n;
};

/*
 * prestate and poststate functions allow the testing environment to be set up
 * on a per-operation basis.
 */
void *prestate(void) {
  struct add_ctx *ctx = malloc(sizeof(struct add_ctx));
  ctx->n = 2;
  return ctx;
}

void poststate(void *ctx) { free(ctx); }

/*
 * Here we define an operation by pulling the required instances from our input
 * space.
 */
void op_add(struct instance *i, void *ctx) {
  int a = unwrap_as_int(instance_get(i, "a"));
  int b = unwrap_as_int(instance_get(i, "b"));
  struct add_ctx *c = ctx;

  int sum = a + b + c->n;
}

struct variant gen_double(void) {
  return wrap((double)rand() / (double)RAND_MAX);
}

struct input_space rand_double_spaces[] = {
    {.name = "a", .gen = gen_double},
    {0},
};

void op_rand_double(struct instance *i, void *ctx) {
  double d = unwrap_as_double(instance_get(i, "a"));

  double x = 100 * d;
}

struct operation ops[] = {
    {
        .name = "add",          /* Name of the operation for output. */
        .fn = op_add,           /* Function to be evaluated. */
        .prestate = prestate,   /* Setup function. */
        .poststate = poststate, /* Teardown function. */
        .space = add_spaces,    /* Function parameters. */
        .props = NULL,          /* Properties to be verified. */
    },
    {
        .name = "rand_double",
        .fn = op_rand_double,
        .space = rand_double_spaces,
    },
    {0},
};

int main() {
  struct specification spec = {.name = "test", .ops = ops};

  run_spec(spec);

  return 0;
}
