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
 * Generator definitions.
 */

GEN(gen_int, rand_r(ctx->seed) % 100)
GEN(gen_str, "Hello, world!")
GEN(gen_ptr_uncaptured, (void *)0xdeadbeef)

struct node *gen_tree_inner(struct gen *ctx, int size) {
  if (!size)
    return NULL;

  struct node *t = tree_init(size);

  size--;
  int split = size ? rand_r(ctx->seed) % (size + 1) : 0;
  int lsize = size - split;
  int rsize = split;

  t->l = gen_tree_inner(ctx, lsize);
  t->r = gen_tree_inner(ctx, rsize);

  return t;
}

struct variant gen_tree(struct gen *ctx, int size) {
  return wrap((void *)(gen_tree_inner(ctx, 10)));
}

int add(int a, int b) { return a + b; }

void *tree_copy_inner(struct arena *a, struct node *t) {
  if (!t)
    return NULL;

  struct node *ptr = aalloc(a, sizeof(struct node));

  assert(ptr);

  memcpy(ptr, t, sizeof(struct node));

  ptr->l = tree_copy_inner(a, t->l);
  ptr->r = tree_copy_inner(a, t->r);

  return ptr;
}

int tree_copy(struct arena *a, struct variant *v) {
  v->ptr = tree_copy_inner(a, v->ptr);
  return 1;
}

void tree_free(struct variant *v) { tree_destroy(v->ptr); }

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
 * Predicate definitions.
 */
PREDICATE(add_commutativity) {

  // Data can be retrieved from the variant look out table.
  int a = given_int(i, "a");
  int b = given_int(i, "b");

  return add(a, b) == add(b, a);
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

PREDICATE(add_tree_example) {
  struct node *t = given_ptr(i, "tree");

  return false;
}

PREDICATE(add_crash_example) {
  abort();
  return true;
}

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
        .iterations = 150,
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
                {.name = "str", .gen = gen_str},
                {.name = "ptr", .gen = gen_ptr_uncaptured},
                {0},
            },
        .seed = 0x18856732,
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

int main() {
  struct specification spec = {.name = "test", .props = props};

  run_spec(spec);

  return 0;
}
