#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kokoki.h"

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)

static int fails_before;
static int fails = 0;
static int success = 0;
KVal top, bot;

#define IS(check_code)                                                         \
  if (!(check_code)) {                                                         \
    fprintf(stderr,                                                            \
            "\n❌ [line: " STRINGIFY(__LINE__) "] FAIL: "                      \
                                               " (check)\n    " STRINGIFY(     \
                                                   check_code) "\n");          \
    fails++;                                                                   \
  } else {                                                                     \
    success++;                                                                 \
  }




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

bool is_str_arr(KVal v, size_t len, const char **strs) {
  if (v.type != KT_ARRAY)
    goto not_array;
  if (v.data.array->size != len)
    goto size_mismatch;
  for (size_t i = 0; i < len; i++) {
    if (!is_str(v.data.array->items[i], strs[i]))
      return false;
  }
  return true;
not_array:
  printf(" expected array of strings\n");
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

// adapted from https://www.forth.com/starting-forth/4-conditional-if-then-statements/
#define eggsize                                                                \
  ": eggsize ( n -- ) "                                                        \
  "   dup 18 < if  \"reject\"      else "                                      \
  "   dup 21 < if  \"small\"       else "                                      \
  "   dup 24 < if  \"medium\"      else "                                      \
  "   dup 27 < if  \"large\"       else "                                      \
  "   dup 30 < if  \"extra large\" else "                                      \
  "   \"error\" "                                                              \
  "   then then then then then nip ; "



void run_native_tests(KCtx *ctx) {
  TEST("comment", "# this is a comment\n 1 2 3 + # and so is this\n+", 1,
       top.data.number == 6);
  TEST("pick1", "1 2 3 0 pick", 4, is_num(top, 3));
  TEST("pick2", "1 2 3 2 pick", 4, is_num(top, 1));
  TEST("pick err", "1 2 42 pick", 3,
       is_error(top, "Stack underflow! (2 < 43)"));
  TEST("move", "1 2 3 1 move", 3, is_num(top, 2) && is_num(bot, 1));
  TEST("move err", "1 move", 1, is_error(top, "Stack underflow! (0 < 2)"));
  TEST("dup", "42 dup", 2, is_num(top, 42));
  TEST("rot", "1 2 3 rot", 3, is_num(top, 1));
  TEST("drop", "1 2 3 drop", 2, is_num(top, 2));
  TEST("swap", "420 69 swap", 2, is_num(top, 420));
  //TEST("basics", "[200.0 200.0 + ] exec 0.67 + 10.01 dup + +", 1, is_num(top, 420.69));

  TEST("define value", ": pi 3.1415 ; 2 pi *", 1, is_num(top, 6.283));
  TEST("define code", ": squared dup * ; 3 squared", 1, is_num(top, 9));

  TEST("compare <", "7 10 <", 1, top.type == KT_TRUE);
  TEST("compare >", "7 10 >", 1, top.type == KT_FALSE);

  TEST("if-then-true", "1 2 < if \"small\" then", 1, is_str(top, "small"));
  TEST("if-then-false", "1 2 > if \"small\" then", 0, true);
  TEST("if-then-else-true", "1 2 < if \"small\" else \"big\" then", 1,
       is_str(top, "small"));
  TEST("if-then-else-false", "10 2 < if \"small\" else \"big\" then", 1,
       is_str(top, "big"));

  TEST("if nested",
       ": howbig dup 100 > if 1000 > if \"very\" then \"big\" then ; "
       "120 howbig",
       1, is_str(top, "big"));

  TEST("if nested both",
       ": howbig dup 100 > if 1000 > if \"very\" then \"big\" then ; "
       "1220 howbig drop",
       1, is_str(top, "very"));

  TEST("eggsize", eggsize " 25 eggsize", 1, is_str(top, "large"));


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

  TEST("sort", "[666 12 42 0] sort", 1,
       is_num_arr(top, 4, (double[]){0, 12, 42, 666}));
  TEST("sort strings1", "[\"foo\" \"Afoobar\"] sort", 1,
       is_str_arr(top, 2, (const char *[]){"Afoobar", "foo"}));
  TEST("sort strings2", "[\"foobar\" \"foo\"] sort", 1,
       is_str_arr(top, 2, (const char *[]){"foo", "foobar"}));


}

void run_stdlib_tests(KCtx *ctx) {

  TEST("nip", "1 2 nip", 1, is_num(top, 2));
  TEST("over", "1 2 over", 3, is_num(top, 1));
  TEST("?dup true", "42 ?dup", 2, is_num(top, 42) && is_num(bot, 42));
  TEST("?dup false", "1 2 > ?dup", 1, top.type == KT_FALSE);
  TEST("?dup nil", "nil ?dup", 1, top.type == KT_NIL);
  TEST("2dup", "1 2 2dup 4 array", 1,
       is_num_arr(top, 4, (double[]){1, 2, 1, 2}));
  TEST("2nip 1", "1 2 3 4 5 2nip 3 array", 1,
       is_num_arr(top, 3, (double[]){1, 4, 5}));
  TEST("tuck", "1 2 tuck", 3, is_num(top, 2) && is_num(bot, 2));
  TEST("2tuck", "1 2 3 4 2tuck 6 array", 1, is_num_arr(top, 6, (double[]){3,4,1,2,3,4}));
  TEST("2nip 2", "1 2 3 4 2nip", 2, is_num(top, 4) && is_num(bot, 3));
  TEST("2over", "1 2 3 4 2over 6 array", 1,
       is_num_arr(top, 6, (double[]){1, 2, 3, 4, 1, 2}));
  TEST("2rot", "1 2 3 4 5 6 2rot 6 array", 1,
       is_num_arr(top, 6, (double[]){3, 4, 5, 6, 1, 2}));
  TEST("2swap", "1 2 3 4 2swap 4 array", 1, is_num_arr(top, 4, (double[]){3,4,1,2}));
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

  TEST("split-at", "\"foo,bar\" ',' split-at", 2,
       is_str(bot, "foo") && is_str(top, "bar"));
  TEST("split-at", "\"nope\" ' ' split-at", 2,
       is_str(bot, "nope") && is_str(top, ""));

  TEST("lines", "\".test/lines.txt\" slurp lines", 1,
       is_str_arr(top, 5,
                  (const char*[]){"first", "second", "third", "",
                                  "fourth after empty"}));

}

void bc_reset(KCtx *ctx) {
  ctx->bytecode->size = 0;
  ctx->pc = 0;
  ctx->stack->size = 0;
}

#define BC(code)                                                               \
  {                                                                            \
    bc_reset(ctx);                                                             \
    {code} emit(ctx, OP_END);                                                  \
    execute(ctx);                                                              \
    if (ctx->stack->size)                                                      \
      top = ctx->stack->items[0];                                              \
  }



void run_bytecode_tests(KCtx *ctx) {
  KVal top;
  BC({ emit(ctx, OP_PUSH_NIL); });
  IS(top.type == KT_NIL);

  BC({ emit(ctx, OP_PUSH_TRUE); });
  IS(top.type == KT_TRUE);

  BC({ emit(ctx, OP_PUSH_FALSE); });
  IS(top.type == KT_FALSE);

  char *hello = "Hello!";
  BC({
    emit(ctx, OP_PUSH_STRING);
    emit_bytes(ctx, 1, (uint8_t[]){6});
    emit_bytes(ctx, 6, (uint8_t *)hello);
  });
  IS(is_str(top, hello));

  char *long_string =
      "this exceeds 255 characters... Lorem ipsum dolor sit amet, consectetuer "
      "adipiscing elit. Sed posuere interdum sem. Quisque ligula eros "
      "ullamcorper quis, lacinia quis facilisis sed sapien. Mauris varius diam "
      "vitae arcu. Sed arcu lectus auctor vitae, consectetuer et venenatis "
      "eget velit. Sed augue orci, lacinia eu tincidunt et eleifend nec lacus. "
      "Donec ultricies nisl ut felis, suspendisse potenti. Lorem ipsum ligula "
      "ut hendrerit mollis, ipsum erat vehicula risus, eu suscipit sem libero "
      "nec erat. Aliquam erat volutpat. Sed congue augue vitae neque. Nulla "
      "consectetuer porttitor pede. Fusce purus morbi tortor magna condimentum "
      "vel, placerat id blandit sit amet tortor.";
  union {
    uint32_t len;
    uint8_t bytes[4];
  } len;
  len.len = strlen(long_string);
  BC({
    emit(ctx, OP_PUSH_STRING_LONG);
    emit_bytes(ctx, 4, len.bytes);
    emit_bytes(ctx, len.len, (uint8_t*)long_string);
  });
  IS(is_str(top, long_string));

  BC({
    emit(ctx, OP_PUSH_INT8);
    emit_bytes(ctx, 1, (uint8_t[]){-42});
  });
  IS(is_num(top, -42));

  union {
    int16_t num;
    uint8_t bytes[2];
  } i16;

  BC({
    emit(ctx, OP_PUSH_INT16);
    i16.num = 12345;
    emit_bytes(ctx, 2, i16.bytes);
  });
  IS(is_num(top, 12345));

  union {
    double num;
    uint8_t bytes[8];
  } num;

  BC({
    emit(ctx, OP_PUSH_NUMBER);
    num.num = 42069.666;
    emit_bytes(ctx, 8, num.bytes);
  });
  IS(is_num(top, 42069.666));

  BC({ emit(ctx, OP_PUSH_ARRAY); });
  IS(top.type == KT_ARRAY && top.data.array->size == 0);

  BC({
    emit(ctx, OP_PUSH_ARRAY);
    emit_bytes(ctx, 9, (uint8_t[]) { OP_PUSH_INT8, 1, OP_APUSH, OP_PUSH_INT8, 2, OP_APUSH, OP_PUSH_INT8, 42, OP_APUSH });
  });
  IS(is_num_arr(top, 3, (double[]){1, 2, 42}));

  BC({
    emit_bytes(
        ctx, 5,
        (uint8_t[]){OP_PUSH_INT8, 42, OP_PUSH_INT8, 7, OP_DIV});
  });
  IS(is_num(top, 6));

  BC({
    emit_bytes(ctx, 5, (uint8_t[]){OP_PUSH_INT8, 42, OP_PUSH_INT8, 7, OP_MUL});
  });
  IS(is_num(top, 294));

  BC({
    emit_bytes(ctx, 5, (uint8_t[]){OP_PUSH_INT8, 42, OP_PUSH_INT8, 7, OP_PLUS});
  });
  IS(is_num(top, 49));

  BC({
    emit_bytes(ctx, 5, (uint8_t[]){OP_PUSH_INT8, 42, OP_PUSH_INT8, 7, OP_MINUS});
  });
  IS(is_num(top, 35));

BC({
    emit_bytes(ctx, 5, (uint8_t[]){OP_PUSH_INT8, 42, OP_PUSH_INT8, 7, OP_MOD});
  });
  IS(is_num(top, 0));


}

void run_tests(KCtx *ctx, void *user) {
  run_native_tests(ctx);
  // run_stdlib_tests(ctx);

  run_bytecode_tests(ctx);
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
