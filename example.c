#include <stdio.h>

#include "invari.h"

static struct variant a_domain[] = {wrap_i(1), wrap_i(2), wrap_i(5),
                                    wrap_none()};
static struct variant b_domain[] = {wrap_i(10), wrap_i(20), wrap_i(50),
                                    wrap_none()};
static struct variant c_domain[] = {wrap_i(100), wrap_i(200), wrap_i(500),
                                    wrap_none()};
static struct domain d[] = {
    {"a", a_domain},
    {"b", b_domain},
    {"c", c_domain},
    {NULL, NULL},
};

/*
 * REMOVE: Placeholder test function
 */
void print_args(struct parameter *p) {
  int a = unwrap_as_int(p[0].v);
  int b = unwrap_as_int(p[1].v);
  int c = unwrap_as_int(p[2].v);

  printf("%d, %d, %d\n", a, b, c);
}

int main() {
  struct test t = {
      .name = "/example/test",
      .fn = print_args,
      .d = d,
  };

  run_args(t);

  return 0;
}
