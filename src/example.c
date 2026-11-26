#include <stdlib.h>
#include <sys/random.h>

#include "invar.h"

/*
 * Generators
 */
GENERATOR(void *, gen_v3) {
  unsigned int *v = malloc(sizeof(int) * 3);
  for (int i = 0; i < 3; i++)
    v[i] = draw_u32(t);

  defer(s, NULL, v);
  return v;
}

/*
 * Properties
 */
PROPERTY(must_pass) {
  draw_u64(t);
  return true;
}

PROPERTY(must_fail_first) { return false; }
PROPERTY(must_fail_later) {
  uint8_t a = draw_u8(t);

  note(t, "a = %d", a);

  return a < 250;
}

PROPERTY(must_abort) { abort(); }

PROPERTY(must_segv_later) {
  uint8_t a = draw_u8(t);

  note(t, "a = %d", a);

  if (a > 250) (void)*(volatile int *)NULL;

  return true;
}

PROPERTY(must_give_up) {
  for (int i = 0; i < 5; i++)
    draw_u8(t);

  return true;
}

PROPERTY(must_discard_some) {
  if (draw_u8(t) < 32)
    for (int i = 0; i < 5; i++)
      draw_u8(t);

  return true;
}

PROPERTY(must_defer) {
  int *v = gen_v3(t, s);
  note(t, "v = [%d, %d, %d]", v[0], v[1], v[2]);
  return true;
}

PROPERTY(must_exit) {
  exit(0);
  return true;
}

PROPERTY(must_be_flaky) {
  unsigned char b;
  getrandom(&b, 1, 0);
  return b & 1;
}

struct property props[] = {
    PROP(must_pass),
    PROP(must_fail_first),
    PROP(must_fail_later),
    PROP(must_abort),
    PROP(must_segv_later, .replays = 1),
    PROP(must_give_up, .capacity = 4),
    PROP(must_discard_some, .capacity = 4),
    PROP(must_defer),
    PROP(must_exit),
    PROP(must_be_flaky),
    {0},

};

struct specification spec = {
    .name  = "test_spec",
    .props = props,
    .cfg =
        {
            .seed       = 0xdeadbeefdeadbeef,
            .iterations = 100,
        },
};

int main() { return verify(&spec); }
