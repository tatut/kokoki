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

bool is_str(KVal v, const char *str) {
  if (v.type != KT_STRING)
    goto not_string;
  if (strlen(str) != v.data.string.len ||
      memcmp(str, v.data.string.data, v.data.string.len) != 0)
    goto not_the_same;
  return true;

 not_string :
  printf(" expected string\n");
  return false;
 not_the_same:
   printf(" expected: %s\n"
          "   actual: %.*s\n",
          str,
          (int)v.data.string.len, v.data.string.data);
   return false;

}

#define age_check                                                              \
  "[ [dup 10 <] \"child\""                                                     \
  "  [dup 25 <] \"young adult\""                                               \
  "  [dup 55 <] \"adult\""                                                     \
  "  true       \"older adult\"] cond"



void run_tests(KCtx *ctx) {
  TEST("comment", "# this is a comment\n 1 2 3 + # and so is this\n+", 1,
       top.data.number == 6);
  TEST("dup", "42 dup", 2, top.data.number == 42);
  TEST("drop", "1 2 3 drop", 2, top.data.number == 2);
  TEST("swap", "420 69 swap", 2, top.data.number == 420);
  TEST("basics", "[200.0 200.0 + ] exec 0.67 + 10.01 dup + +", 1, top.data.number == 420.69);

  TEST("define value", ": pi 3.1415 ; 2 pi *", 1, top.data.number == 6.283);
  TEST("define code", ": squared dup * ; 3 squared", 1, top.data.number == 9);

  TEST("compare <", "7 10 <", 1, top.type == KT_TRUE);
  TEST("compare >", "7 10 >", 1, top.type == KT_FALSE);

  //TEST("cond err", "42 cond", 1, is_error(top, "Cond requires an array with alternating condition/action pairs."));
  TEST("cond1", "7 " age_check, 2, is_str(top, "child"));
  TEST("cond2", "22 " age_check, 2, is_str(top, "young adult"));
  TEST("cond3", "44 " age_check, 2, is_str(top, "adult"));
  TEST("cond fallback", "123 " age_check, 2, is_str(top, "older adult"));

}
int main(int argc, char **argv) {
  kokoki_init(run_tests);
  if (fails) {
    fprintf(stderr, "%d failures!\n", fails);
    return 1;
  }
  return 0;
}
