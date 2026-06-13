#include <stdio.h>
#include <stdlib.h>

#include "invari.h"

struct variant gen_int(void) {
  return wrap(rand() % 100);
}

/*
 * Data can be threaded through the testing environment using custom structs in
 * tandem with the (pre/post)state functions.
 */
struct add_ctx {
  int n;
};

/*
 * prestate and poststate functions allow the testing environment to be set up
 * on a per-property basis.
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

struct property props[] = {
    {
        .name = "add",          /* Name of the property for reporting. */
        .predicate = NULL,      /* Predicate to be tested. */
        .space = NULL,          /* Function parameters. */
        .prestate = prestate,   /* Setup function. */
        .poststate = poststate, /* Teardown function. */
    },
    {
        .name = "rand_double",
        .space = NULL,
    },
    {0},
};

int main() {
  struct specification spec = {.name = "test", .props = props};

  run_spec(spec);

  return 0;
}
