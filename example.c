#include <stdio.h>
#include <stdlib.h>

#include "invari.h"

struct variant gen_int(void) {
  return wrap(rand() % 100);
}

int add(int a, int b) { return a + b; }

/*
 * Data can be threaded through the testing environment using custom structs in
 * tandem with the (pre/post)state functions.
 */
struct add_ctx {
  int n;
};

/*
 * The prestate and poststate functions allow the testing environment to be set
 * up on a per-property basis.
 */
void *prestate(void) {
  struct add_ctx *ctx = malloc(sizeof(struct add_ctx));
  ctx->n = 2;
  return ctx;
}

void poststate(void *ctx) { free(ctx); }

struct variant gen_double(void) {
  return wrap((double)rand() / (double)RAND_MAX);
}

bool add_commutativity(struct instance *i, void *ctx) {

  /*
   * Data can be retrieved from the variant look out table.
   */
  int a = unwrap_as_int(instance_get(i, "a"));

  /*
   * Or through the ctx pointer we set up in our prestate.
   */
  struct add_ctx *c = ctx;

  return add(a, c->n) == add(c->n, a);
}

bool add_associativity(struct instance *i, void *ctx) {
  int a = unwrap_as_int(instance_get(i, "a"));
  int b = unwrap_as_int(instance_get(i, "b"));
  int c = unwrap_as_int(instance_get(i, "c"));

  return add(add(a, b), c) == add(a, add(b, c));
}

bool add_identity(struct instance *i, void *ctx) {
  int a = unwrap_as_int(instance_get(i, "a"));

  return add(a, 0) == a;
}

bool add_inverse(struct instance *i, void *ctx) {
  int a = unwrap_as_int(instance_get(i, "a"));

  return add(a, -a) == 0;
}

bool add_less_than_50(struct instance *i, void *ctx) {
  int a = unwrap_as_int(instance_get(i, "a"));

  return a < 50;
}

bool add_crash_example(struct instance *i, void *ctx) {
  abort();
  return true;
}

struct property props[] = {
    {
        .name = "commutativity", /* Name of the property for reporting. */
        .predicate = add_commutativity, /* Predicate to be tested. */
        .space =
            (struct input_space[]){
                {.name = "a", gen_int},
                {0},
            },                  /* Function parameters. */
        .prestate = prestate,   /* Setup function. */
        .poststate = poststate, /* Teardown function. */
    },
    {
        .name = "associativity",
        .predicate = add_associativity,
        .space =
            (struct input_space[]){
                {.name = "a", gen_int},
                {.name = "b", gen_int},
                {.name = "c", gen_int},
                {0},
            },
    },
    {
        .name = "identity",
        .predicate = add_identity,
        .space =
            (struct input_space[]){
                {.name = "a", gen_int},
                {0},
            },
        .iter = 150,
    },
    {
        .name = "inverse",
        .predicate = add_inverse,
        .space =
            (struct input_space[]){
                {.name = "a", gen_int},
                {0},
            },
    },
    {
        .name = "less_than_50",
        .predicate = add_less_than_50,
        .space =
            (struct input_space[]){
                {.name = "a", gen_int},
                {0},
            },
        .seed = 0x0068663b,
    },
    {
        .name = "crash_example",
        .predicate = add_crash_example,
        .space = (struct input_space[]){{0}},
    },
    {0},
};

int main() {
  struct specification spec = {.name = "test", .props = props};

  run_spec(spec);

  return 0;
}
