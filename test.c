#include <stdio.h>
#include <string.h>
#include "kokoki.h"

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)

static int fails_before;
static int fails = 0;
static int success = 0;
KVal top, bot;

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
    if (ctx->stack->size) {                                                    \
      top = ctx->stack->items[ctx->stack->size - 1];                           \
      bot = ctx->stack->items[0];                                              \
    }                                                                          \
    if (!(check_code)) {                                                       \
      fprintf(stderr,                                                          \
              "\n❌ [line: " STRINGIFY(__LINE__) "] FAIL: " name               \
                                                 " (check)\n    " STRINGIFY(   \
                                                     check_code) "\n");        \
      fails++;                                                                 \
    }                                                                          \
    if (fails == fails_before) {                                               \
      /*printf("✅ SUCCESS: " name "\n");*/                                    \
      printf("✅ ");                                                           \
      success++;                                                               \
    } else {                                                                   \
      printf("STACK:");                                                        \
      for (size_t i = 0; i < ctx->stack->size; i++) {                          \
        printf(" ");                                                           \
        kval_dump(ctx->stack->items[i]);                                       \
      }                                                                        \
      printf("\n");                                                            \
    }                                                                          \
    fflush(stdout);                                                            \
    ctx->stack->size = 0;                                                      \
  }




bool is_error(KVal v, const char *err) {
  if (v.type != KT_ERROR) {
    printf(" expected error, got: ");
    kval_dump(v);
    printf("\n");
    return false;
  }
  if (strlen(err) != v.data.string.len ||
      memcmp(err,v.data.string.data, v.data.string.len) != 0) {
    printf(" error with text\n"
           " expected: %s\n"
           "   actual: %.*s\n", err, (int)v.data.string.len, v.data.string.data);
    return false;
  }
  return true;
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

bool is_num_arr(KVal v, size_t len, double *nums) {
  if (v.type != KT_ARRAY)
    goto not_array;
  if (v.data.array->size != len)
    goto size_mismatch;
  for (size_t i = 0; i < len; i++) {
    if (v.data.array->items[i].data.number != nums[i]) {
      printf("  [%zu] expected %f, got %f\n", i, nums[i],
             v.data.array->items[i].data.number);
      return false;
    }
  }
  return true;
 not_array:
  printf(" expected array\n");
  return false;
size_mismatch:
  printf(" expected array of length %zu, got length %zu\n", len,
         v.data.array->size);
  return false;
}

bool is_num(KVal v, double num) {
  if (v.type != KT_NUMBER) {
    printf(" expected number\n");
    return false;
  }
  if (v.data.number != num) {
    printf(" expected %f, got %f\n", num, v.data.number);
    return false;
  }
  return true;
}

#define age_check                                                              \
  "[ [dup 10 <] \"child\""                                                     \
  "  [dup 25 <] \"young adult\""                                               \
  "  [dup 55 <] \"adult\""                                                     \
  "  true       \"older adult\"] cond"

void run_native_tests(KCtx *ctx) {
  TEST("comment", "# this is a comment\n 1 2 3 + # and so is this\n+", 1,
       top.data.number == 6);
  TEST("pick1", "1 2 3 0 pick", 4, is_num(top, 3));
  TEST("pick2", "1 2 3 2 pick", 4, is_num(top, 1));
  TEST("pick err", "1 2 42 pick", 3, is_error(top, "Can't pick item 42 from stack that has size 2"));
  TEST("dup", "42 dup", 2, is_num(top, 42));
  TEST("rot", "1 2 3 rot", 3, is_num(top, 1));
  TEST("drop", "1 2 3 drop", 2, is_num(top, 2));
  TEST("swap", "420 69 swap", 2, is_num(top, 420));
  TEST("basics", "[200.0 200.0 + ] exec 0.67 + 10.01 dup + +", 1, is_num(top, 420.69));

  TEST("define value", ": pi 3.1415 ; 2 pi *", 1, is_num(top, 6.283));
  TEST("define code", ": squared dup * ; 3 squared", 1, is_num(top, 9));

  TEST("compare <", "7 10 <", 1, top.type == KT_TRUE);
  TEST("compare >", "7 10 >", 1, top.type == KT_FALSE);

  //TEST("cond err", "42 cond", 1, is_error(top, "Cond requires an array with alternating condition/action pairs."));
  TEST("cond1", "7 " age_check, 2, is_str(top, "child"));
  TEST("cond2", "22 " age_check, 2, is_str(top, "young adult"));
  TEST("cond3", "44 " age_check, 2, is_str(top, "adult"));
  TEST("cond fallback", "123 " age_check, 2, is_str(top, "older adult"));

  TEST("slurp", "\".test/small.txt\" slurp", 1,
       is_str(top, "Korvatunturin Konkatenatiivinen Kieli\n"));

  TEST("each", "[1 2 3] [2 *] each", 1,
       is_num_arr(top, 3, (double[]){2, 4, 6}));
  TEST("each2", ": inc 1 + ; [41 665] [inc] each", 1,
       is_num_arr(top, 2, (double[]){42, 666}));

  TEST("fold", "[1 2 3 0] [+] fold", 1, is_num(top, 6));
  TEST("fold1", "[42] [+] fold", 1, is_num(top, 42));
  TEST("cat", "\"foo\" \"bar\" cat", 1, is_str(top, "foobar"));
  TEST("cat num 1", "\"foo\" 33 cat", 1, is_str(top, "foo!"));
  TEST("cat num 2", "33 \"foo\" cat", 1, is_str(top, "!foo"));

  TEST("fold cat", "[\"foo\" \"bar\" \"baz\"] [cat] fold", 1,
       is_str(top, "foobarbaz"));

  TEST("filter even", "[1 2 3 6 8 41] [2 % 0 =] filter", 1,
       is_num_arr(top, 3, (double[]){2, 6, 8}));
  TEST("not1", "1 2 < not", 1, top.type == KT_FALSE);
  TEST("not2", "false not", 1, top.type == KT_TRUE);
  TEST("not3", "nil not", 1, top.type == KT_TRUE);
  TEST("not4", "42 not", 1, top.type == KT_FALSE);

  TEST("apush", "[ 1 2 ] 3 apush", 1, is_num_arr(top, 3, (double[]){1, 2, 3}));
  TEST("len", "[1 2 3] len", 2, is_num(top, 3));
  TEST("aget", "[1 2 3] 1 aget", 2, is_num(top, 2));
  TEST("aget str", "\"foo!\" 3 aget", 2, is_num(top,33));
  TEST("aset", "[1 2 3] 1 42 aset", 1,
       is_num_arr(top, 3, (double[]){1, 42, 3}));
  TEST("aset end", "[1 2] 2 3 aset", 1, is_num_arr(top, 3, (double[]){1,2,3}));
  TEST("aget oob", "[1 2] 5 aget", 2,
       is_error(top, "Index out of bounds 5 (0 - 1 inclusive)"));
  TEST("adel", "[1 2 3 4] 2 adel", 1, is_num_arr(top, 3, (double[]){1,2,4}));

  TEST("times1", "3 4 times + + +", 1, is_num(top, 12));
  TEST("times2", "[] [6 apush] 3 times", 1,
       is_num_arr(top, 3, (double[]){6, 6, 6}));

  TEST("read new ref", "@foo ?", 1, top.type == KT_NIL);
  TEST("write ref", "@foo 42 !", 0, true);
  TEST("write+read ref", "[] @foo 42 ! @foo ? apush", 1,
       is_num_arr(top, 1, (double[]){42}));
  TEST("read multiple", "@x 666 ! @x ? @x ? =", 1, top.type == KT_TRUE);
  TEST("swap ref", "@x 40 ! @x [2 +] !! @x ?", 1, is_num(top, 42));
  TEST("swap ref value", "@x 4.2 ! @x [10 *] !?", 1, is_num(top, 42));

  TEST("eval", "\"4.2 10 *\" eval", 1, is_num(top, 42));

  TEST("and1", "1 2 and", 1, top.type == KT_TRUE);
  TEST("and2", "1 false and", 1, top.type == KT_FALSE);
  TEST("and3", "true 42 and", 1, top.type == KT_TRUE);

  TEST("rev", "[1 2 3] reverse", 1, is_num_arr(top, 3, (double[]){3, 2, 1}));
  TEST("rev str", "\"foobar\" reverse", 1, is_str(top, "raboof"));

}

void run_stdlib_tests(KCtx *ctx) {
  kokoki_eval(ctx, "\"stdlib.ki\" use");

  TEST("nip", "1 2 nip", 1, is_num(top, 2));
  TEST("over", "1 2 over", 3, is_num(top, 1));
  TEST("?dup true", "42 ?dup", 2, is_num(top, 42) && is_num(bot, 42));
  TEST("?dup false", "1 2 > ?dup", 1, top.type == KT_FALSE);
  TEST("?dup nil", "nil ?dup", 1, top.type == KT_NIL);
  TEST("2dup", "1 2 2dup 4 array", 1,
       is_num_arr(top, 4, (double[]){1, 2, 1, 2}));
  TEST("2nip 1", "1 2 3 4 5 2nip 3 array", 1,
       is_num_arr(top, 3, (double[]){1, 4, 5}));
  TEST("2nip 2", "1 2 3 4 2nip", 2, is_num(top, 4) && is_num(bot, 3));
  TEST("if1", "1 2 < \"yes\" if", 1, is_str(top, "yes"));
  TEST("if2", "3 2 < \"yes\" if", 0, 1);

  TEST("if-else1",
       ": is-adult? 25 > \"is adult\" \"not an adult\" if-else ; "
       "44 is-adult?",
       1, is_str(top, "is adult"));
  TEST("if-else2",
       ": is-adult? 25 > \"is adult\" \"not an adult\" if-else ; "
       "18 is-adult?",
       1, is_str(top, "not an adult"));

  TEST("str->int", "\"420\" str->int", 1, is_num(top, 420));

}

void run_tests(KCtx *ctx, void *user) {
  run_native_tests(ctx);
  run_stdlib_tests(ctx);
}

int main(int argc, char **argv) {
  kokoki_init(run_tests,NULL);
  printf("\n%d success\n", success);
  if (fails) {
    fprintf(stderr, "%d failures!\n", fails);
    return 1;
  }
  return 0;
}
