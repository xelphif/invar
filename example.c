#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "invari.h"

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

/*
 * Generator definitions.
 */

GEN(gen_int, rand() % 100)
GEN(gen_double, (double)rand() / (double)RAND_MAX)
GEN(gen_str, "Hello, world!");

/*
 * Predicate definitions.
 */
PREDICATE(add_commutativity) {

  // Data can be retrieved from the variant look out table.
  int a = given_int(i, "a");

  // Or through the ctx pointer we set up in our prestate.
  struct add_ctx *c = ctx;

  return add(a, c->n) == add(c->n, a);
}

PREDICATE(add_associativity) {
  int a = given_int(i, "a");
  int b = given_int(i, "b");
  int c = given_int(i, "c");

  return add(add(a, b), c) == add(a, add(b, c));
}

PREDICATE(add_identity) {
  int a = given_int(i, "a");

  return add(a, 0) == a;
}

PREDICATE(add_inverse) {
  int a = given_int(i, "a");

  return add(a, -a) == 0;
}

PREDICATE(add_less_than_50) {
  int a = given_int(i, "a");

  return a < 50;
}

PREDICATE(add_crash_example) {
  abort();
  return true;
}

struct property props[] = {
    {
        .name = "commutativity", /* Name of the property for reporting. */
        .predicate = add_commutativity, /* Predicate to be tested. */
        .prestate = prestate,           /* Setup function. */
        .poststate = poststate,         /* Teardown function. */
        .space = SPACE{{.name = "a", gen_int}, {0}},

    },
    {
        .name = "associativity",
        .predicate = add_associativity,
        .space =
            SPACE{
                {.name = "a", gen_int},
                {.name = "b", gen_int},
                {.name = "c", gen_int},
                {0},
            },
    },
    {
        .name = "identity",
        .predicate = add_identity,
        .space = SPACE{{.name = "a", gen_int}, {0}},
        .iterations = 150,
    },
    {
        .name = "inverse",
        .predicate = add_inverse,
        .space = SPACE{{.name = "a", gen_int}, {0}},
    },
    {
        .name = "less_than_50",
        .predicate = add_less_than_50,
        .space =
            SPACE{
                {.name = "a", gen_int},
                {.name = "str", gen_str},
                {0},
            },
        .seed = 0x0068663b,
    },
    {
        .name = "crash_example",
        .predicate = add_crash_example,
        .space = SPACE{{.name = "a", gen_int}, {0}},
    },
    {0},
};

int main() {
  struct specification spec = {.name = "test", .props = props};

  run_spec(spec);

  return 0;
}
