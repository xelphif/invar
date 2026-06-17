#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "invari.h"

/*
 * Primitive tree data structure.
 */
struct node {
  int n;
  struct node *l;
  struct node *r;
};

void *tree_init(int n) {
  struct node *t = malloc(sizeof(struct node));
  t->n = n;
  return t;
}

void tree_destroy(struct node *t) {
  if (!t)
    return;

  tree_destroy(t->l);
  tree_destroy(t->r);

  free(t);
}

/*
 * Generator and sample function definitions.
 */

SAMPLE(sample_int, rand_r(seed) % 100)

struct gen gen_int = {
    .name = "gen_int",
    .sample = sample_int,
};

// TODO: FIXED STR VARIANT

// TODO: FIXED PTR VARIANT

/*
 * Generate a random tree of maximum 'size' nodes.
 */
struct node *sample_tree(struct gen *self, unsigned int *seed, int size) {
  if (!size)
    return NULL;

  struct node *t = tree_init(size);

  size--;
  int split = size ? rand_r(seed) % (size + 1) : 0;
  int lsize = size - split;
  int rsize = split;

  t->l = sample_tree(self, seed, lsize);
  t->r = sample_tree(self, seed, rsize);

  return t;
}

struct variant sample_tree_wrap(struct gen *self, unsigned int *seed,
                                int size) {
  return wrap((void *)(sample_tree(self, seed, size)));
}

struct gen gen_tree = {
    .name = "gen_tree",
    .sample = sample_tree_wrap,
    .size = 10,
    .types = TYPES{sizeof(struct node), -1},
};

/*
 * Copy tree to shared memory for counterexample reporting.
 */
void *tree_copy_inner(struct arena *a, struct node *t) {
  if (!t)
    return NULL;

  // TODO: APUSH {
  struct node *ptr = aalloc(a, sizeof(struct node));

  assert(ptr);

  memcpy(ptr, t, sizeof(struct node));
  // }

  ptr->l = tree_copy_inner(a, t->l);
  ptr->r = tree_copy_inner(a, t->r);

  return ptr;
}

int tree_copy(struct arena *a, struct variant *v) {
  v->ptr = tree_copy_inner(a, v->ptr);
  return 1;
}

void tree_free(struct variant *v) { tree_destroy(v->ptr); }

/*
 * Print tree to render for counterexample.
 */
void tree_print_inner(struct node *t) {
  if (!t) {
    printf("nil");
    return;
  }

  printf("%d", t->n);

  if (!t->l && !t->r)
    return;

  printf(" (");
  tree_print_inner(t->l);
  printf(" ");
  tree_print_inner(t->r);
  printf(")");
}
void tree_print(struct variant *v) { tree_print_inner(v->ptr); }

/*
 * Addition function for testing.
 */
int add(int a, int b) { return a + b; }

/*
 * Predicate definitions.
 */
PREDICATE(add_commutativity) {

  // Data can be retrieved from the variant look out table.
  int a = given_int(table, "a");
  int b = given_int(table, "b");

  return add(a, b) == add(b, a);
}

PREDICATE(add_associativity) {
  int a = given_int(table, "a");
  int b = given_int(table, "b");
  int c = given_int(table, "c");

  return add(add(a, b), c) == add(a, add(b, c));
}

PREDICATE(add_identity) {
  int a = given_int(table, "a");

  return add(a, 0) == a;
}

PREDICATE(add_inverse) {
  int a = given_int(table, "a");

  return add(a, -a) == 0;
}

PREDICATE(add_less_than_50) {
  int a = given_int(table, "a");

  return a < 50;
}

PREDICATE(add_tree_example) {
  struct node *t = given_ptr(table, "tree");

  return false;
}

PREDICATE(add_crash_example) {
  abort();
  return true;
}

int main() {

  struct property props[] = {
      {
          .name = "commutativity", /* Name of the property for reporting. */
          .predicate = add_commutativity, /* Predicate to be tested. */
          .space =
              SPACE{
                  {.name = "a", .gen = gen_int},
                  {.name = "b", .gen = gen_int},
                  {0},
              },

      },
      {
          .name = "associativity",
          .predicate = add_associativity,
          .space =
              SPACE{
                  {.name = "a", .gen = gen_int},
                  {.name = "b", .gen = gen_int},
                  {.name = "c", .gen = gen_int},
                  {0},
              },
      },
      {
          .name = "identity",
          .predicate = add_identity,
          .space = SPACE{{.name = "a", .gen = gen_int}, {0}},
          .cfg =
              {
                  .iterations = 150,
              },
      },
      {
          .name = "inverse",
          .predicate = add_inverse,
          .space = SPACE{{.name = "a", .gen = gen_int}, {0}},
      },
      {
          .name = "less_than_50",
          .predicate = add_less_than_50,
          .space =
              SPACE{
                  {.name = "a", .gen = gen_int},
                  /* {.name = "str", .gen = gen_str}, */
                  /* {.name = "ptr", .gen = sample_ptr}, */
                  {0},
              },
          .cfg =
              {
                  .seed = 0x18856732,
              },
      },
      {
          .name = "tree_print_example",
          .predicate = add_tree_example,
          .space =
              SPACE{
                  {.name = "tree",
                   .gen = gen_tree,
                   .copy = tree_copy,
                   .free = tree_free,
                   .print = tree_print},
                  {0},
              },
      },
      {
          .name = "crash_example",
          .predicate = add_crash_example,
          .space = SPACE{{.name = "a", .gen = gen_int}, {0}},
      },
      {0},
  };

  struct specification spec = {
      .name = "test",
      .props = props,
  };

  run_spec(spec);

  return 0;
}
