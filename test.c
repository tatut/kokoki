#include <stdio.h>
#include <string.h>
#include "kokoki.h"

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)

static int fails_before;
static int fails = 0;
KVal top;

#define TEST(name, src, expected_stack_size, check_code)                       \
  {                                                                            \
    kokoki_eval(ctx, src);                                                     \
    fails_before = fails;                                                      \
    if (ctx->stack->size != (expected_stack_size)) {                           \
      fprintf(stderr,                                                          \
              "❌ FAIL: " name                                                 \
              " (stack size mismatch!)\n expected %d\n actual  %zu\n",         \
              (expected_stack_size), ctx->stack->size);                        \
      fails++;                                                                 \
    }                                                                          \
    if (ctx->stack->size)                                                      \
      top = ctx->stack->items[ctx->stack->size - 1];                           \
    if (!(check_code)) {                                                       \
      fprintf(stderr,                                                          \
              "❌ FAIL: " name " (check)\n    " STRINGIFY(check_code) "\n");   \
      fails++;                                                                 \
    }                                                                          \
    if (fails == fails_before) {                                               \
      printf("✅ SUCCESS: " name "\n");                                        \
    }                                                                          \
    ctx->stack->size = 0;                                                      \
  }


bool is_error(KVal v, const char *err) {
  if (v.type != KT_ERROR)
    return false;
  if (strlen(err) != v.data.string.len)
    return false;
  return memcmp(err,v.data.string.data, v.data.string.len) == 0;
}


void run_tests(KCtx *ctx) {
  TEST("basics", "[200.0 200.0 + ] exec 0.67 + 10.01 dup + +", 1, top.data.number == 420.69);

  TEST("define value", ": pi 3.1415 ; 2 pi *", 1, top.data.number == 6.283);
  TEST("define code", ": squared dup * ; 3 squared", 1, top.data.number == 9);
}
int main(int argc, char **argv) {
  kokoki_init(run_tests);
  if (fails) {
    fprintf(stderr, "%d failures!\n", fails);
    return 1;
  }
  return 0;
}
