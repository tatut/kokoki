/* kokoki = Korvatunturin Konkatenatiivinen Kieli
 * A Forth like programming language.
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "tgc/tgc.h"
#include "kokoki.h"
#include "color.h"
#include <assert.h>

static tgc_t gc;

#define RES2H_ALLOC(size) tgc_alloc(&gc, (size))
#include "stdlib.h"

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)


const char *TYPE_NAME[] = {
    [KT_NIL] = "nil",
    [KT_FALSE] = "false",
    [KT_TRUE] = "true",
    [KT_NUMBER] = "number",
    [KT_STRING] = "string",
    [KT_NAME] = "name",
    [KT_ARRAY_START] = "[ ",
    [KT_ARRAY_END] = " ]",
    [KT_REF_NAME] = "refname",
    [KT_ERROR] = "error",
    [KT_BLOCK] = "block",
    [KT_DEF_START] = "definition start",
    [KT_DEF_END] = "definition end",
    [KT_REF_VALUE] = "refvalue",
    [KT_CODE_ADDR] = "code addre",
    [KT_EOF] = "EOF",
    [KT_HASHMAP] = "hashmap",
    [KT_NATIVE] = "native",
    [KT_COMMA] = ", "};

/*
  syntax:
  42             push number to stack
  "merry xmas"   push string to stack
  [1 2 3]        push array with 3 elements to stack
  foo            execute word 'foo'
  : pi 3.1415 ;  define word 'pi'
  {"foo" 42}     push hashmap with 1 mapping to stack
  nil            push nil value to stack
  true           push boolean true to stack
  false          push boolean false to stack
  foobar         execute name foobar

  predefined words: (stack size change)
  basic:
  dup       (+1)       duplicate top element
  drop      (-1)       discard top element
  swap      (0)        change the order of the top 2 elements
  exec      (-1)       take top element and execute it

  numbers:       (-1)
  (a b) + - * / %      plus, minus, multiplication, division, modulo
  (a b) < <= > >= =    comparisons

  arrays:
  (arr val) (-1) apush       push top value end of array
  (arr idx) (-1) adel        delete value at index, moving the rest over
  (arr)     (+1) apop        remove last value from array

  hashmaps:
  (hm key val) (-2) hmput    add mapping for value
  (hm key)      (0) hmget    get value for key, leaves value as top
  (hm key)     (-1) hmdel    delete mapping for key

  strings:
  (filename) (0)    slurp    fully read given file and place the contents on
stack
  (str)      (0)    lines    split string into array of lines
  (str str)  (-1)   split    split string by separator into array of tokens
  (arr str)  (-1)   join     join array of strings with separator
  (str str)  (-1)   cat      join 2 strings

 */


// hash from https://stackoverflow.com/a/69812981
#define SEED 0x12345678
uint32_t hash_str(KString str) {
  uint32_t h = SEED;
  for (size_t i = 0; i < str.len; i++) {
    h ^= str.data[i];
    h *= 0x5bd1e995;
    h ^= h >> 15;
  }
  return h;
}

uint32_t hash_ptr(void *ptr) {
  // hash pointer as 8 chars ¯\_(ツ)_/¯
  return hash_str((KString){.len = sizeof(void*), .data = (char*)ptr});
}
uint32_t hash_num(double num) {
  return hash_str((KString){.len = sizeof(double), .data = (char*)&num});
}

uint32_t kval_hash(KVal v) { // MurmurOAAT_32
  switch (v.type) {
  case KT_FALSE:
    return 0;
  case KT_TRUE:
    return 1;
  case KT_NIL:
    return -1;
  case KT_STRING:
  case KT_NAME:
  case KT_REF_NAME:
    return hash_str(v.data.string);
  case KT_ARRAY:
    return hash_ptr(v.data.array);
  case KT_HASHMAP:
    return -42069; // FIXME
  case KT_NUMBER: {
    return hash_num(v.data.number);
  }
  case KT_NATIVE:
    return hash_ptr(v.data.native);
  case KT_REF_VALUE:
    return hash_ptr(v.data.ref);

  default: return 0;// ERROR, EOF, KT_DEFINIION don't have hash
  }

}

#define VA_ARGS(...) , ##__VA_ARGS__
#define err(val, fmt, ...)                                                     \
  {                                                                            \
    (val).type = KT_ERROR;                                                     \
    size_t _errlen = (size_t)snprintf(NULL, 0, fmt VA_ARGS(__VA_ARGS__));      \
    (val).data.string.len = _errlen;                                           \
    (val).data.string.data = tgc_alloc(&gc, _errlen + 1);                      \
    snprintf((val).data.string.data, _errlen + 1, fmt VA_ARGS(__VA_ARGS__));   \
  }

bool falsy(KVal v) { return (v.type == KT_FALSE || v.type == KT_NIL); }

bool kval_eq(KVal a, KVal b) {
  if (a.type != b.type)
    return false;
  switch (a.type) {
    // true, false, nil => same if type matches
  case KT_TRUE:
  case KT_FALSE:
  case KT_NIL:
  case KT_EOF:
    return true;

    // string, number, check value
  case KT_STRING:
  case KT_NAME:
  case KT_ERROR:
  case KT_REF_NAME:
    if (a.data.string.len != b.data.string.len)
      return false;
    return memcmp(a.data.string.data, b.data.string.data, a.data.string.len) ==
           0;
  case KT_REF_VALUE:
    return a.data.ref == b.data.ref;

  case KT_NUMBER:
    return a.data.number == b.data.number;

    // arrays and hashmaps, compare contents
  case KT_ARRAY:
    if (a.data.array->size != b.data.array->size)
      return false;
    for (size_t i = 0; i < a.data.array->size; i++) {
      if (!kval_eq(a.data.array->items[i], b.data.array->items[i])) {
        return false;
      }
    }
    return true;

  case KT_HASHMAP:
    // FIXME
    return false;

  case KT_NATIVE:
    return a.data.native == b.data.native;

    // KT_DEFINITION and ERROR are not comparable
  default:
    return false;
  }
}

void kval_dump(KVal v);

void hm_put(KHashMap *hm, KVal key, KVal value) {
  uint32_t hash = kval_hash(key);
  size_t idx;
  if (hm->size == hm->capacity) {
    size_t new_capacity = hm->capacity == 0 ? 64 : hm->capacity * 1.62;
    // we need to rehash
    KHashMapEntry *new_items = tgc_calloc(&gc, new_capacity, sizeof(KHashMapEntry));
    if (!new_items) {
      fprintf(stderr, "Out of memory for hashmap\n");
      exit(1);
    }
    KHashMapEntry *old_items = hm->items;
    size_t old_size = hm->size;

    hm->capacity = new_capacity;
    hm->items = new_items;
    hm->size = 0;

    for (size_t i = 0; i < old_size; i++) {
      hm_put(hm, old_items[i].key, old_items[i].value);
    }
    if(old_items) tgc_free(&gc, old_items);
  }
  idx = hash % hm->capacity;
  size_t orig_idx = idx;
  while(hm->items[idx].used && !kval_eq(key, hm->items[idx].key)) {
    idx = (idx + 1) % hm->capacity;
    if (idx == orig_idx) {
      fprintf(stderr, "hm_put failed, no free space anywhere in table!\n");
      return;
    }
  }
  hm->items[idx] = (KHashMapEntry){.key = key, .value = value, .used = true};
  hm->size++;
}

KVal hm_get(KHashMap *hm, KVal key) {
  if (!hm->size)
    return (KVal){.type = KT_NIL};
  uint32_t hash = kval_hash(key);
  size_t idx = hash % hm->capacity;
  size_t orig_idx = idx;
  //printf("\n----%d search for: ", key.type); kval_dump(key); printf("- hash: %d, idx: %zu----\n", hash, idx);
  while (hm->items[idx].used) {
    /*printf("eq? ");
    kval_dump(key);
    printf(" vs: ");
    kval_dump(hm->items[idx].key);
    printf(" ? %s\n", kval_eq(key, hm->items[idx].key) ? "true" : "false");
    */
    if (kval_eq(key, hm->items[idx].key))
      return hm->items[idx].value;
    idx = (idx + 1) % hm->capacity;
    if(idx == orig_idx) break; // gone through whole table
  }
  return (KVal){.type=KT_NIL};
}

KCtx *kctx_new() {
  KCtx *ctx = tgc_calloc(&gc, 1, sizeof(KCtx));
  ctx->names = tgc_calloc(&gc, 1, sizeof(KHashMap));
  ctx->stack = tgc_calloc(&gc, 1, sizeof(KArray));
  ctx->bytecode = tgc_calloc(&gc, 1, sizeof(KByteCode));
  ctx->return_addr = tgc_calloc(&gc, 1, sizeof(KAddrStack));
  return ctx;
}



/* Parsing */

void next(KReader *in) {
  // don't go past EOF
  if (in->at == in->end)
    return;
  char ch = *in->at;
  if(ch == 0) return;

  in->at++;
  if (ch == '\n') {
    in->line++;
    in->col = 1;
  } else {
    in->col++;
  }
}
void skip(KReader *in, size_t count) {
  for(size_t i=0;i<count;i++) next(in);
}

char peek(KReader *in) {
  if(in->at == in->end) return 0;
  return *(in->at + 1);
}
char at(KReader *in) {
  if(in->at == in->end) return 0;
  return *in->at;
}

void skipws(KReader *in) {
 start: {
    char c = at(in);
    while(c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      next(in);
      c = at(in);
    }
    if (c == '#') {
      while (at(in) != '\n' && at(in) != 0) next(in);
      goto start;
    } else if (c == '(') {
      while (at(in) != ')' && at(in) != 0)
        next(in);
      next(in);
      goto start;
    }
  }
}

bool is_alpha(char ch) {
  return (ch >= 'a' && ch <= 'z') ||
    (ch >= 'A' && ch <= 'Z');
}

bool is_digit(char ch) { return ch >= '0' && ch <= '9'; }

bool is_alphanumeric(char ch) { return is_alpha(ch) || is_digit(ch); }

bool is_name_start_char(char ch) {
  return is_alpha(ch) || ch == '_' || ch == '$' || ch == '+' || ch == '<' ||
         ch == '>' || ch == '=' || ch == '?' || ch == '.' || ch == '*' ||
         ch == '%' || ch == '!' ;
}

bool is_name_char(char ch) {
  return is_name_start_char(ch) || is_digit(ch) || ch == '-';
}

bool looking_at(KReader *in, char *word) {
  char *start = in->at;
  while(*word != 0) {
    char c = at(in);
    if (c == 0 || c != *word) {
      in->at = start;
      return false;
    }
    next(in);
    word++;
  }
  return true;
}

KVal copy_str(KType type, char *start, char *end) {
  size_t len = (size_t)(end - start);
  KVal s = (KVal){.type = type,
                  .data.string = {.len = len, .data = tgc_alloc(&gc, len)}};
  memcpy(s.data.string.data, start, len);
  return s;
}

KVal read_str(KReader *in) {
  char *start = in->at + 1;
  char *end = start;
  while (*end != '"') end++;
  in->at = end + 1;
  return copy_str(KT_STRING, start, end);
}

KVal read_name(KReader *in) {
  char *start = in->at;
  char *end = start;
  while (is_name_char(*end)) end++;
  in->at = end;
  return copy_str(KT_NAME, start, end);
}

KVal read_ref(KReader *in) {
  char *start = in->at + 1;
  char *end = start;
  while (is_name_char(*end))
    end++;
  in->at = end;
  return copy_str(KT_REF_NAME, start, end);
}

KVal read_num(KReader *in) {
  double mult = 1.0;
  double val = 0;
  if (at(in) == '-') {
    mult = -1.0;
    next(in);
  }
  while (is_digit(at(in))) {
    val = 10 * val + (at(in) - 48);
    next(in);
  }
  if (at(in) == '.') {
    // has fraction
    next(in);
    double frac = 0, div = 1;
    while (is_digit(at(in))) {
      frac = 10 * frac + (at(in) - 48);
      div *= 10;
      next(in);
    }
    val += (frac / div);
  }
  return (KVal){.type = KT_NUMBER, .data.number = mult*val};
}

KVal read(KReader *in);

void arr_push(KArray *arr, KVal v) {
  ARR_PUSH(arr, v);
}

void arr_remove_first(KArray *arr) { ARR_REMOVE_FIRST(arr); }

KVal arr_remove_nth(KArray *arr, size_t idx) {
  KVal item = arr->items[idx];
  for (size_t i = idx; i < arr->size - 1; i++) {
    arr->items[i] = arr->items[i+1];
  }
  arr->size -= 1;
  return item;
}

const char *ERR_STACK_UNDERFLOW = "Stack underflow!";
bool check_underflow(KArray *arr, KVal *val) {
  if(arr->size == 0) {
    *val = (KVal){.type = KT_ERROR,
                  .data.string = {.len = strlen(ERR_STACK_UNDERFLOW),
                                  .data = (char *)ERR_STACK_UNDERFLOW}};
    return false;
  }
  return true;
}

KVal arr_pop(KArray *arr) {
  KVal ret;
  if (check_underflow(arr, &ret)) {
    ret = arr->items[--arr->size];
  }
  return ret;
}

KVal arr_peek(KArray *arr) {
  KVal ret;
  if (check_underflow(arr, &ret)) {
    ret = arr->items[arr->size - 1];
  }
  return ret;
}


KVal read(KReader *in) {
  skipws(in);
  KVal out;
  switch (at(in)) {
  case 0:
    out = (KVal){.type = KT_EOF};
    break;
  case '@':
    out = read_ref(in);
    break;
  case '"':
    out = read_str(in);
    break;
  case '0': case '1': case '2': case '3': case '4': case '5':
  case '6': case '7': case '8': case '9':
    if (is_alpha(peek(in))) {
      // to support names like "2dup" that start with number
      out = read_name(in);
      break;
    } else {
      out = read_num(in);
      break;
    }
  case '-':
    if (is_digit(peek(in))) {
      out = read_num(in);
      break;
    } else {
      out = read_name(in);
      break;
    }

  case '\'': {
    next(in);
    if (peek(in) != '\'')
      goto fail;
    char ch = at(in);
    next(in);
    next(in);
    out = (KVal){.type = KT_NUMBER, .data.number = ch}; break;
  }
  case 't': {
    if (looking_at(in, "true")) {
      skip(in, 4);
      out = (KVal){.type = KT_TRUE};
      break;
    }
    out = read_name(in);
    break;
  }
  case 'f': {
    if (looking_at(in, "false")) {
      skip(in, 5);
      out = (KVal){.type = KT_FALSE};
      break;
    }
    out = read_name(in);
    break;
  }
  case 'n': {
    if (looking_at(in, "nil")) {
      skip(in, 3);
      out = (KVal){.type = KT_NIL};
      break;
    }
    out = read_name(in);
    break;
  }
  case ':':
    next(in);
    out = (KVal){.type = KT_DEF_START};
    break;
  case ';':
    next(in);
    out = (KVal){.type = KT_DEF_END};
    break;

  case '[':
    next(in);
    out = (KVal){.type = KT_ARRAY_START};
    break;
  case ']':
    next(in);
    out = (KVal){.type = KT_ARRAY_END};
    break;

  case ',':
    next(in);
    out = (KVal){.type = KT_COMMA};
    break;

  default:
    if (is_name_start_char(at(in))) {
      out = read_name(in);
      break;
    }
  }
  in->last_token = out;
  return out;

 fail: {
    int err_len = snprintf(NULL, 0, "Parse error on line %d, col %d: '%c'     ", in->line, in->col, at(in));
    char *err = tgc_alloc(&gc, err_len);
    snprintf(err, err_len, "Parse error on line %d, col %d: '%c'", in->line, in->col, at(in));
    next(in);
    in->last_token =
        (KVal){.type = KT_ERROR, .data.string = {.len = err_len - 1, err}};
    return in->last_token;
 }

}

void kval_dump(KVal v) {
  switch (v.type) {
  case KT_NIL:
    col(PURPLE);
    printf("nil");
    break;
  case KT_TRUE:
    col(RED);
    printf("true");
    break;
  case KT_FALSE:
    col(RED);
    printf("false");
    break;
  case KT_STRING:
    col(GREEN);
    printf("%.*s", (int) v.data.string.len, v.data.string.data);
    break;
  case KT_NAME:
    printf("%.*s", (int)v.data.string.len, v.data.string.data);
    break;
  case KT_REF_NAME:
    printf("@%.*s", (int)v.data.string.len, v.data.string.data);
    break;
  case KT_REF_VALUE:
    printf("#<Ref: ");
    kval_dump(v.data.ref->value);
    printf(">");
    break;
  case KT_NUMBER:
    col(YELLOW);
    if ((v.data.number - (long)v.data.number) == 0.0) {
      printf("%ld", (long)v.data.number);
    } else {
      printf("%f", v.data.number);
    }
    break;

  case KT_ARRAY_START:
    printf("[ ");
    break;
  case KT_ARRAY_END:
    printf(" ]");
    break;

  case KT_ARRAY:
    printf("[");
    for (size_t i = 0; i < v.data.array->size; i++) {
      if (i > 0)
        printf(" ");
      kval_dump(v.data.array->items[i]);
    }
    printf("]");
    break;

  case KT_DEF_START:
    printf(": ");
    break;
  case KT_DEF_END:
    printf(" ; ");
    break;

  case KT_BLOCK:
    printf("{");
    for (size_t i = 0; i < v.data.array->size; i++) {
      if (i > 0)
        printf(" ");
      kval_dump(v.data.array->items[i]);
    }
    printf("}");
    break;

  case KT_HASHMAP_START:
    printf("{ ");
    break;
  case KT_HASHMAP_END:
    printf(" }");
    break;
  case KT_NATIVE:
    printf("#<native function %p>", v.data.native);
    break;
  case KT_HASHMAP:
    printf("#<hashmap fixme>");
    break;
  case KT_ERROR:
    printf("#<ERROR: %.*s>", (int)v.data.string.len, v.data.string.data);
    break;
  case KT_EOF:
    printf("#<EOF>");
    break;

  case KT_CODE_ADDR:
    printf("#<compiled code @ %d>", v.data.address);
    break;
  }
  reset();
}

void debug_stack(KCtx *ctx) {
  for (size_t i = 0; i < ctx->stack->size; i++) {
   printf(" ");
   kval_dump(ctx->stack->items[i]);
  }
}

void debug_exec(KCtx *ctx, KVal v) {
  printf("EXECUTING %d: ", v.type);
  kval_dump(v);
  printf(" STACK:");
  debug_stack(ctx);
  printf("\n");
}

void exec(KCtx *ctx, KVal v) {
  //debug_exec(ctx, v);
  switch (v.type) {
  case KT_NAME: {
    KVal name = hm_get(ctx->names, v);
    if (name.type == KT_NIL) {
      fprintf(stderr, "Undefined name: %.*s\n", (int)v.data.string.len,
              v.data.string.data);
    } else {
      exec(ctx, name);
    }
    break;
  }
  case KT_NATIVE: {
    void(*fn)(KCtx *) = v.data.native;
    fn(ctx);
    break;
  }
  case KT_NIL:
  case KT_TRUE:
  case KT_FALSE:
  case KT_NUMBER:
  case KT_STRING:
  case KT_ARRAY:
  case KT_REF_NAME:
    arr_push(ctx->stack, v);
    break;

    /* FIXME: remove this whole function */

  case KT_BLOCK: {
    for (size_t i = 0; i < v.data.array->size; i++) {
      exec(ctx, v.data.array->items[i]);
    }
    break;
  }

  default:
    fprintf(stderr, "Can't execute type: %d\n", v.type);

  }
}

KVal *kval_new(KVal v) {
  KVal *kv = tgc_alloc(&gc, sizeof(KVal));
  if(!kv) {
    fprintf(stderr, "Out of memory!");
    exit(1);
  }
  memcpy(kv, &v, sizeof(KVal));
  return kv;
}

typedef struct KNative {
  const char *name;
  void (*fn)(KCtx *);
  KOp op; // if no impl, then use this opcode
  uint16_t index;
} KNative;

void (*get_native_impl(uint16_t index))(KCtx*);
bool get_native_fn(const char *name, KNative *nat);

/* Execute bytecode */
void execute(KCtx *ctx) {
#define NEXT() ctx->bytecode->items[ctx->pc++]
#define ASSERT_STACK(n)                                                        \
  if (ctx->stack->size < (n)) {                                                \
    min_stack = (n);                                                           \
    goto underflow;                                                            \
  }

  union { int16_t n; uint8_t b[2]; } i16;
  union { uint16_t n; uint8_t b[2]; } u16;
  union { double n; uint8_t b[8]; } num;
  union { uint32_t n; uint8_t b[4]; } u32;

  size_t min_stack;

  for(;;) {
    uint8_t op;
    op = NEXT();
    printf("[PC:%u] OP %d\n", ctx->pc - 1, op);
    KVal push;
    switch (op) {
    case OP_END:
      return;
    case OP_PUSH_NIL:
      push = (KVal){.type = KT_NIL};
      goto push_it;
    case OP_PUSH_TRUE:
      push = (KVal){.type = KT_TRUE};
      goto push_it;
    case OP_PUSH_FALSE:
      push = (KVal){.type = KT_FALSE};
      goto push_it;
    case OP_PUSH_INT8:
      push = (KVal){.type = KT_NUMBER,
                    .data.number = (int8_t)NEXT()};
      goto push_it;
    case OP_PUSH_INT16:
      i16.b[0] = NEXT();
      i16.b[1] = NEXT();
      push = (KVal){.type = KT_NUMBER, .data.number = i16.n};
      goto push_it;
    case OP_PUSH_NUMBER:
      memcpy(num.b, &ctx->bytecode->items[ctx->pc], 8);
      ctx->pc += 8;
      push = (KVal){.type = KT_NUMBER, .data.number = num.n};
      goto push_it;
    case OP_PUSH_STRING:
    case OP_PUSH_NAME:
      push = (KVal){.type = (op == OP_PUSH_NAME ? KT_NAME : KT_STRING),
                    .data.string.len = NEXT()};
      push.data.string.data = tgc_alloc(&gc, push.data.string.len);
      memcpy(push.data.string.data, &ctx->bytecode->items[ctx->pc], push.data.string.len);
      ctx->pc += push.data.string.len;
      printf("pushing short string: \"%.*s\"\n", (int) push.data.string.len, push.data.string.data);
      goto push_it;
    case OP_PUSH_STRING_LONG:
      memcpy(u32.b, &ctx->bytecode->items[ctx->pc], 4);
      ctx->pc += 4;
      push =
          (KVal){.type = KT_STRING,
                 .data.string = {.len = u32.n, .data = tgc_alloc(&gc, u32.n)}};
      memcpy(push.data.string.data, &ctx->bytecode->items[ctx->pc], u32.n);
      ctx->pc += u32.n;
      goto push_it;
    case OP_PUSH_ARRAY:
      push = (KVal){.type = KT_ARRAY, .data.array = tgc_calloc(&gc, 1, sizeof(KArray))};
      goto push_it;
    case OP_APUSH: {
      KVal item = arr_pop(ctx->stack);
      KVal arr = arr_peek(ctx->stack);
      arr_push(arr.data.array, item);
      break;
    }
#define BINARY_OP(op)                                                          \
  {                                                                            \
    ASSERT_STACK(2);                                                           \
    KVal b = arr_pop(ctx->stack);                                              \
    KVal a = arr_pop(ctx->stack);                                              \
    push = (KVal){.type = KT_NUMBER,                                           \
                  .data.number = a.data.number op b.data.number};              \
    goto push_it;                                                              \
  }
    case OP_PLUS: BINARY_OP(+);
    case OP_MINUS: BINARY_OP(-);
    case OP_MUL: BINARY_OP(*);
    case OP_DIV: BINARY_OP(/);

#define BINARY_BOOL_OP(OP)                                                     \
  {                                                                            \
    ASSERT_STACK(2);                                                           \
    KVal b = arr_pop(ctx->stack);                                              \
    KVal a = arr_pop(ctx->stack);                                              \
    bool res = a.data.number OP b.data.number;                                 \
    push = (KVal){.type = res ? KT_TRUE : KT_FALSE};                           \
    goto push_it;                                                              \
  }

    case OP_LT: BINARY_BOOL_OP(<);
    case OP_GT: BINARY_BOOL_OP(>);
    case OP_LTE: BINARY_BOOL_OP(<=);
    case OP_GTE: BINARY_BOOL_OP(>=);

    case OP_MOD: {
      ASSERT_STACK(2);
      KVal b = arr_pop(ctx->stack);
      KVal a = arr_pop(ctx->stack);
      push = (KVal){.type = KT_NUMBER, .data.number = (double)((long)a.data.number % (long)b.data.number)};
      goto push_it;
    }
    case OP_SHL: {
      ASSERT_STACK(2);
      KVal b = arr_pop(ctx->stack);
      KVal a = arr_pop(ctx->stack);
      push = (KVal){.type = KT_NUMBER,
                    .data.number =
                        (double)((long)a.data.number << (long)b.data.number)};
      goto push_it;
    }
    case OP_SHR: {
      ASSERT_STACK(2);
      KVal b = arr_pop(ctx->stack);
      KVal a = arr_pop(ctx->stack);
      push = (KVal){.type = KT_NUMBER,
                    .data.number =
                        (double)((long)a.data.number >> (long)b.data.number)};
      goto push_it;
    }
    case OP_AND: {
      ASSERT_STACK(2);
      KVal b = arr_pop(ctx->stack);
      KVal a = arr_pop(ctx->stack);
      push = (KVal){.type = (!falsy(a) && !falsy(b)) ? KT_TRUE : KT_FALSE};
      goto push_it;
    }
    case OP_OR: {
      ASSERT_STACK(2);
      KVal b = arr_pop(ctx->stack);
      KVal a = arr_pop(ctx->stack);
      push = (KVal){.type = (!falsy(a) || !falsy(b)) ? KT_TRUE : KT_FALSE};
      goto push_it;
    }

    case OP_EQ: {
      ASSERT_STACK(2);
      KVal b = arr_pop(ctx->stack);
      KVal a = arr_pop(ctx->stack);
      push = (KVal){.type = kval_eq(a, b) ? KT_TRUE : KT_FALSE};
      goto push_it;
    }

      /* Basic stack manipulation */
    case OP_DUP:
      ASSERT_STACK(1);
      push = arr_peek(ctx->stack);
      goto push_it;
    case OP_DROP:
      ASSERT_STACK(1);
      arr_pop(ctx->stack);
      break;
    case OP_SWAP: {
      ASSERT_STACK(2);
      KVal tmp = ctx->stack->items[ctx->stack->size - 1];
      ctx->stack->items[ctx->stack->size - 1] = ctx->stack->items[ctx->stack->size - 2];
      ctx->stack->items[ctx->stack->size - 2] = tmp;
      break;
    }
    case OP_ROT: {
      ASSERT_STACK(3);
      KVal tmp = ctx->stack->items[ctx->stack->size - 3];
      ctx->stack->items[ctx->stack->size - 3] = ctx->stack->items[ctx->stack->size - 2];
      ctx->stack->items[ctx->stack->size - 2] = ctx->stack->items[ctx->stack->size - 1];
      ctx->stack->items[ctx->stack->size - 1] = tmp;
      break;
    }
    case OP_OVER: {
      ASSERT_STACK(2);
      push = ctx->stack->items[ctx->stack->size - 2];
      goto push_it;
    }
    case OP_NIP:
      ASSERT_STACK(2);
      ctx->stack->items[ctx->stack->size - 2] = ctx->stack->items[ctx->stack->size - 1];
      ctx->stack->size -= 1;
      break;
    case OP_TUCK:
      ASSERT_STACK(2);
      push = ctx->stack->items[ctx->stack->size - 1];
      ctx->stack->items[ctx->stack->size - 1] =
          ctx->stack->items[ctx->stack->size - 2];
      ctx->stack->items[ctx->stack->size - 2] = push;
      goto push_it;

    case OP_MOVEN:
    case OP_MOVE1:
    case OP_MOVE2:
    case OP_MOVE3:
    case OP_MOVE4:
    case OP_MOVE5: {
      size_t n = (op == OP_MOVEN ? (size_t)arr_pop(ctx->stack).data.number
                                 : op - OP_MOVEN);
      ASSERT_STACK(n+1);
      KVal tmp = ctx->stack->items[ctx->stack->size - n - 1];
      for (size_t i = (ctx->stack->size - n - 1); i < ctx->stack->size - 1; i++)
        ctx->stack->items[i] = ctx->stack->items[i + 1];
      ctx->stack->items[ctx->stack->size-1] = tmp;
      break;
    }

    case OP_PICKN:
    case OP_PICK1:
    case OP_PICK2:
    case OP_PICK3:
    case OP_PICK4:
    case OP_PICK5: {
      size_t n = (op == OP_PICKN ? (size_t)arr_pop(ctx->stack).data.number
                                 : op - OP_PICKN);
      ASSERT_STACK(n+1);
      push = ctx->stack->items[ctx->stack->size - n - 1];
      goto push_it;
    }

    case OP_JMP:
    case OP_CALL: {
      uint32_t addr = NEXT();
      addr = (addr << 8) + NEXT();
      addr = (addr << 8) + NEXT();
      if(op == OP_CALL) {
        printf("calling to %d\n", addr);
        ARR_PUSH(ctx->return_addr, ctx->pc);
      }
      ctx->pc = addr;
      break;
    }
    case OP_JMP_TRUE:
    case OP_JMP_FALSE: {
      ASSERT_STACK(1);
      KVal cond = arr_pop(ctx->stack);
      printf("check if ");
      kval_dump(cond);
      printf(" %s ?\n", op == OP_JMP_TRUE ? "truthy" : "falsy");
      bool f = falsy(cond);
      if((op == OP_JMP_TRUE && !f) || (op == OP_JMP_FALSE && f)) {
        uint32_t addr = NEXT();
        addr = (addr << 8) + NEXT();
        addr = (addr << 8) + NEXT();
        printf("  jumping to %d!\n", addr);
        ctx->pc = addr;
      } else {
        ctx->pc += 3;
        printf(" not jumping, pc: %u\n", ctx->pc);

      }
      break;
    }
    case OP_RETURN: {
      ctx->pc = ctx->return_addr->items[--ctx->return_addr->size];
      break;
    }
    case OP_INVOKE: {
      uint16_t idx = NEXT();
      idx = (idx << 8) + NEXT();
      void (*impl)(KCtx*) = get_native_impl(idx);
      impl(ctx);
      break;
    }
    case OP_PRINT:
      kval_dump(arr_pop(ctx->stack));
      break;
    default:
      fprintf(stderr, "Unknown bytecode op: %d\n", op);
      exit(1);
    }
    continue;
  underflow:
    err(push, "Stack underflow! (%zu < %zu)", ctx->stack->size, min_stack);
  push_it:
    ARR_PUSH(ctx->stack, push);
    continue;

  }
#undef NEXT
#undef ASSERT_STACK
}

void emit_bytes(KCtx *ctx, size_t len, uint8_t *bytes) {
  for (size_t i = 0; i < len; i++) {
    ARR_PUSH(ctx->bytecode, bytes[i]);
  }
}

void emit(KCtx *ctx, KOp op) { emit_bytes(ctx, 1, (uint8_t[]){op}); }

bool is_name(KVal v, const char *name) {
  if (v.type != KT_NAME)
    return false;
  size_t len = strlen(name);
  if (len != v.data.string.len)
    return false;
  return memcmp(v.data.string.data, name, len) == 0;
}

void emit_val(KCtx *ctx, KVal val) {
  switch (val.type) {
  case KT_NIL:
    emit(ctx, OP_PUSH_NIL);
    return;
  case KT_TRUE:
    emit(ctx, OP_PUSH_TRUE);
    return;
  case KT_FALSE:
    emit(ctx, OP_PUSH_FALSE);
    return;
  case KT_NUMBER: {
    if (val.data.number - (long)val.data.number == 0) {
      if (val.data.number >= -128 && val.data.number <= 127) {
        emit(ctx, OP_PUSH_INT8);
        emit(ctx, (int8_t)val.data.number);
        return;
      } else if (val.data.number >= -32768 && val.data.number <= 32767) {
        emit(ctx, OP_PUSH_INT16);
        union { int16_t n; uint8_t b[2]; } i16;
        i16.n = val.data.number;
        emit_bytes(ctx, 2, i16.b);
        return;
      }
    }
    emit(ctx, OP_PUSH_NUMBER);
    union { double n; uint8_t b[8]; } num;
    num.n = val.data.number;
    emit_bytes(ctx, 8, num.b);
    return;
  }
  case KT_STRING: {
    if (val.data.string.len <= 255) {
      emit(ctx, OP_PUSH_STRING);
      emit(ctx, (uint8_t) val.data.string.len);
    } else {
      emit(ctx, OP_PUSH_STRING_LONG);
      union { uint32_t len; uint8_t b[4]; } len;
      len.len = val.data.string.len;
      emit_bytes(ctx, 4, len.b);
    }
    emit_bytes(ctx, val.data.string.len, (uint8_t*)val.data.string.data);
    return;
  }
  default:
    fprintf(stderr, "Compilation error, can't emit value of type: %s\n",
            TYPE_NAME[val.type]);

  }
}

typedef enum CompileMode {
  C_TOPLEVEL,   // compile everything until EOF
  C_DEFINITION, // compiling word def, wait for ';'
  C_ARRAY,      // compiling array literal item, wait for comma or ']'
  C_HASHMAP,    // compiling hashmap literal item, wait for comma or '}'
  C_IF,         // compiling IF, waiting for ELSE or THEN
  C_IF_ELSE     // compiling after IF ... ELSE, waiting for THEN
} CompileMode;


static int compile_depth=0;
/* Read tokens from in and compile it to bytecode.
 * Sets program counter to start of compiled bytecode.
 */
void compile(KCtx *ctx, KReader *in, CompileMode mode) {
  compile_depth++;
  size_t pc = ctx->pc;
  if (pc && (mode == C_TOPLEVEL)) {
    if (ctx->bytecode->items[pc - 1] != OP_END) {
      fprintf(stderr,
              "Existing bytecode in bad state, expected empty or END, got %d\n",
              ctx->bytecode->items[pc - 1]);
      exit(1);
    }
    ctx->bytecode->size -= 1; // overwrite the END opcode
    ctx->pc -= 1; // start executing from that
  }
  KVal token;
  KVal next;
  token = read(in);
  bool empty = true;
  for (;;) {
    // Check exit condition
    switch (mode) {
    case C_TOPLEVEL:
      if (token.type == KT_EOF)
        goto done;
      break;
    case C_DEFINITION:
      if (token.type == KT_DEF_END)
        goto done;
      break;
    case C_ARRAY:
      if (token.type == KT_COMMA || token.type == KT_ARRAY_END)
        goto done;
      break;
    case C_HASHMAP:
      if (token.type == KT_COMMA || token.type == KT_HASHMAP_END)
        goto done;
      break;
    case C_IF:
      if (is_name(token, "else") || is_name(token, "then"))
        goto done;
      break;
    case C_IF_ELSE:
      if (is_name(token, "then"))
        goto done;
      break;
    }
    if (token.type == KT_EOF) {
      // EOF should only come at toplevel, otherwise it is unexpected
      fprintf(stderr, "Compilation failed, unexpected EOF");
      return;
    }
    empty = false;
    /*printf("COMPILE>> ");
    kval_dump(token);
    printf(" <<\n");*/
    switch (token.type) {
    case KT_NIL:
    case KT_FALSE:
    case KT_TRUE:
    case KT_STRING:
      emit_val(ctx, token);
      break;
    case KT_NUMBER: {
      // encountered a number, either push it as constant
      // or lookahead the next, if we can encode a pick/move instruction
      long n = (long)token.data.number;
      if (token.data.number - n == 0 && n > 0 && n <= 5) {
        next = read(in);
        if (is_name(next, "pick")) {
          emit(ctx, OP_PICKN+n);
        } else if (is_name(next, "move")) {
          emit(ctx, OP_MOVEN + n);
        } else {
          // not suitable for encoding
          emit_val(ctx, token);
          token = next;
          continue;
        }
      } else {
        emit_val(ctx, token);
      }
      break;
    }
    case KT_NAME: {
      /* Check special control structures */

      if (is_name(token, "if")) {
        printf("compile if, current depth: %d\n", compile_depth);
        // next up is either else block or then block, depending if ending
        // token is "else" or "then", reserve space for a jump
        size_t before_pos = ctx->bytecode->size;
        emit_bytes(ctx, 4, (uint8_t[]){0, 0, 0, 0});
        // nested compile
        compile(ctx, in, C_IF);
        if (is_name(in->last_token, "then")) {
          // jmp over the then block if false
          size_t after_pos = ctx->bytecode->size;
          printf("before %zu compile jmp false to %zu\n", before_pos, after_pos);
          ctx->bytecode->items[before_pos + 0] = OP_JMP_FALSE;
          ctx->bytecode->items[before_pos + 1] = after_pos >> 16;
          ctx->bytecode->items[before_pos + 2] = after_pos >> 8;
          ctx->bytecode->items[before_pos + 3] = after_pos;
        } else if (is_name(in->last_token, "else")) {
          // jump to else block if false
          // reserve space to jump over then block
          size_t after_then_pos = ctx->bytecode->size;
          emit_bytes(ctx, 4, (uint8_t[]){0, 0, 0, 0});
          size_t else_pos = ctx->bytecode->size;
          compile(ctx, in, C_IF_ELSE);
          if (!is_name(in->last_token, "then")) {
            fprintf(stderr,
                    "Compilation failed: expected 'then' to end if "
                    "statement, got: %s\n",
                    TYPE_NAME[in->last_token.type]);
            kval_dump(in->last_token);
            printf("\n");
            return;
          }

          ctx->bytecode->items[before_pos + 0] = OP_JMP_FALSE;
          ctx->bytecode->items[before_pos + 1] = else_pos >> 16;
          ctx->bytecode->items[before_pos + 2] = else_pos >> 8;
          ctx->bytecode->items[before_pos + 3] = else_pos;

          // after then block, jump over else block
          uint32_t after_else_pos = ctx->bytecode->size;
          printf(" after else pos %u\n", after_else_pos);
          ctx->bytecode->items[after_then_pos + 0] = OP_JMP;
          ctx->bytecode->items[after_then_pos + 1] = after_else_pos >> 16;
          ctx->bytecode->items[after_then_pos + 2] = after_else_pos >> 8;
          ctx->bytecode->items[after_then_pos + 3] = after_else_pos;

        } else {
          fprintf(stderr, "if/else/then failed, unexpected token: %s\n",
                  TYPE_NAME[in->last_token.type]);
          return;
        }

        break;
      }

      /* name, must be previously defined in this context, a native function
       * or a special operator
       */
      //printf("check if name defined\n");
      KVal addr = hm_get(ctx->names, token);
      if (addr.type == KT_CODE_ADDR) {
        // emit CALL to given code addr
        emit(ctx, OP_CALL);
        emit(ctx, (uint8_t)(addr.data.address >> 16));
        emit(ctx, (uint8_t)(addr.data.address >> 8));
        emit(ctx, (uint8_t)addr.data.address);
        break;
      }
      //printf("check if this is native\n");
      char buf[255];
      snprintf(buf, 255, "%.*s", (int)token.data.string.len,
               token.data.string.data);
      uint16_t native_fn;
      KNative native;
      if (get_native_fn(buf, &native)) {
        if (native.fn) {
          //printf("  native fn\n");
          emit(ctx, OP_INVOKE);
          emit(ctx, (uint8_t)(native.index >> 8));
          emit(ctx, (uint8_t)(native.index));
        } else {
          //printf("  native OP: %d\n", native.op);
          emit(ctx, native.op);
        }
        break;
      }
      fprintf(stderr, "Compilation error, undefined word: %.*s\n", (int)token.data.string.len, token.data.string.data);
      break;
    }
    case KT_DEF_START: {
      /* compile word definition */
      // jump over the definition when executing
      size_t jump_over_pos_idx = ctx->bytecode->size + 1;
      emit(ctx, OP_JMP);
      // 3 byte address after the RET call, to be filled after compilation
      emit(ctx, 0);
      emit(ctx, 0);
      emit(ctx, 0);
      size_t start = ctx->bytecode->size;
      KVal name = read(in);
      /*printf(" compile word def: ");
      kval_dump(name);
      printf("\n");*/
      if (name.type != KT_NAME) {
        fprintf(stderr, "Compilation failed, expected name for definition.");
        kval_dump(name);
        return;
      }
      compile(ctx, in, C_DEFINITION);
      //printf("   done, addr: %zu\n", start);
      uint32_t jump_over_pos = ctx->bytecode->size;
      ctx->bytecode->items[jump_over_pos_idx + 0] = jump_over_pos >> 16;
      ctx->bytecode->items[jump_over_pos_idx + 1] = jump_over_pos >> 8;
      ctx->bytecode->items[jump_over_pos_idx + 2] = jump_over_pos;
      hm_put(ctx->names, name, (KVal){.type = KT_CODE_ADDR, .data.address = start});
      break;
    }
    case KT_ARRAY_START: {
      // push new array onto stack
      emit(ctx, OP_PUSH_ARRAY);
      // compile array items until last token is ']'
      do {
        compile(ctx, in, C_ARRAY);

      } while (in->last_token.type == KT_COMMA);

      if (in->last_token.type != KT_ARRAY_END) {
        fprintf(stderr, "Compilation failed, expected array end, got: %s\n",
                TYPE_NAME[in->last_token.type]);
        return;
      }
      break;
    }

    default:
      fprintf(stderr,
              "FIXME: compilation failed on line %d, col %d, token type: %s\n",
              in->line, in->col,
              TYPE_NAME[token.type]);
      kval_dump(token);
    }
    token = read(in);
  }
 done:
  switch (mode) {
  case C_TOPLEVEL:
    emit(ctx, OP_END);
    break;
  case C_DEFINITION:
    emit(ctx, OP_RETURN);
    break;
  case C_ARRAY:
    if (!empty)
      emit(ctx, OP_APUSH);
    break;
  case C_HASHMAP:
    if (!empty)
      emit(ctx, OP_HMPUT);
    break;
  default: break;
  }
  compile_depth--;
  //printf("----\n");
}


#define IN(name, typename)                                                     \
  KVal name = arr_pop(ctx->stack);                                             \
  if (name.type != typename) {                                                 \
    KVal errv;                                                                 \
    err(errv, "Expected type " STRINGIFY(type));                               \
    arr_push(ctx->stack, errv);                                                \
    return;                                                                    \
  }


#define IN_ANY(name) KVal name = arr_pop(ctx->stack)
#define IN_EXEC(name)                                                          \
  KVal name = arr_pop(ctx->stack);                                             \
  if (name.type == KT_ARRAY)                                                   \
  name.type = KT_BLOCK

#define OUT(v) arr_push(ctx->stack, (v))

void native_nl(KCtx *ctx) { printf("\n"); }

void native_slurp(KCtx *ctx) {
  char filename[512];
  KVal name = arr_pop(ctx->stack);
  KVal err;
  if (name.type != KT_STRING) {
    err(name, "Slurp requires a string filename");
    arr_push(ctx->stack, err);
    return;
  } else if (name.data.string.len > 511) {
    err(name, "Too long filename");
    arr_push(ctx->stack, err);
    return;
  }
  snprintf(filename, 512, "%.*s", (int)name.data.string.len,
           name.data.string.data);
  struct stat b;
  stat(filename, &b);
  FILE* f = fopen(filename, "r");
  char* in = (char*) tgc_alloc(&gc, b.st_size+1);
  fread(in, b.st_size, 1, f);
  in[b.st_size] = 0;
  fclose(f);
  arr_push(ctx->stack, (KVal){.type = KT_STRING,
                              .data.string = {.len = b.st_size, .data = in}});
}


bool is_uint8_num(KVal n) {
  return n.type == KT_NUMBER && (long) n.data.number >= 0 &&
    (long) n.data.number <= 255;
}

void native_cat(KCtx *ctx) {
  KVal b = arr_pop(ctx->stack);
  KVal a = arr_pop(ctx->stack);
  KVal error;
  if (b.type == KT_STRING && a.type == KT_STRING) {
    size_t len = a.data.string.len + b.data.string.len;
    KVal str = (KVal) {
      .type = KT_STRING, .data.string = {
        .len = len,
        .data = tgc_alloc(&gc, len)
      }
    };
    memcpy(str.data.string.data, a.data.string.data, a.data.string.len);
    memcpy(&str.data.string.data[a.data.string.len], b.data.string.data, b.data.string.len);
    OUT(str);
  } else if (a.type == KT_STRING && is_uint8_num(b)) {
    // append char to a
    size_t len = a.data.string.len + 1;
    KVal str = (KVal){.type = KT_STRING,
                      .data.string = {.len = len, .data = tgc_alloc(&gc, len)}};
    memcpy(str.data.string.data, a.data.string.data, len - 1);
    str.data.string.data[len-1] = (uint8_t) b.data.number;
    OUT(str);
  } else if (is_uint8_num(a) && b.type == KT_STRING) {
    // prepend char to b
    size_t len = b.data.string.len + 1;
    KVal str = (KVal){.type = KT_STRING,
                      .data.string = {.len = len, .data = tgc_alloc(&gc, len)}};
    str.data.string.data[0] = (uint8_t)a.data.number;
    memcpy(&str.data.string.data[1], b.data.string.data, len - 1);
    OUT(str);
  } else {
    err(error, "Expected two strings or a string and a number (0-255) to join");
    arr_push(ctx->stack, error);
  }

}

int kval_compare(const void *Aptr, const void *Bptr) {
  KVal a = *((KVal *)Aptr);
  KVal b = *((KVal *)Bptr);
  if (a.type != b.type) {
    return a.type - b.type;
  }
  switch (a.type) {
  case KT_NUMBER:
    return a.data.number < b.data.number
               ? -1
               : (a.data.number > b.data.number ? 1 : 0);
  case KT_STRING: {
    size_t al = a.data.string.len, bl = b.data.string.len;
    int ord = memcmp(a.data.string.data, b.data.string.data, al < bl ? al : bl);
    if (ord == 0) {
      return al - bl;
    } else {
      return ord;
    }
  }
  case KT_ARRAY: {
    size_t al = a.data.array->size, bl = b.data.array->size;
    if (al != bl) {
      return al - bl;
    } else {
      for (size_t i = 0; i < al; i++) {
        int ord =
            kval_compare(&a.data.array->items[i], &b.data.array->items[i]);
        if (ord != 0)
          return ord;
      }
      return 0;
    }
  }
  default: return 0;
  }
}

void native_sort(KCtx *ctx) {
  IN(arr, KT_ARRAY);
  qsort(arr.data.array->items, arr.data.array->size, sizeof(KVal),
        kval_compare);
  OUT(arr);
}
void native_compare(KCtx *ctx) {
  IN_ANY(b);
  IN_ANY(a);
  KVal c =(KVal){.type = KT_NUMBER, .data.number = kval_compare(&a,&b)};
  OUT(c);
}

void native_len(KCtx *ctx) {
  IN_ANY(arr);
  KVal len;
  if (arr.type == KT_ARRAY) {
    len = (KVal){.type = KT_NUMBER, .data.number = arr.data.array->size};
  } else if (arr.type == KT_STRING) {
    len = (KVal){.type = KT_NUMBER, .data.number = arr.data.string.len};
  } else {
    KVal len;
    err(len, "Expected array or string for len");
  }
  OUT(arr);
  OUT(len);
}

void native_aget(KCtx *ctx) {
  KVal idx = arr_pop(ctx->stack);
  KVal arr = arr_peek(ctx->stack);
  KVal ret;

  if (arr.type != KT_ARRAY && arr.type != KT_STRING) {
    err(ret, "Expected array or string to get from");
  } else if (idx.type != KT_NUMBER) {
    err(ret, "Expected number index to get");
  } else {
    size_t len =
      arr.type == KT_ARRAY ? arr.data.array->size : arr.data.string.len;
    size_t i = (size_t) idx.data.number;
    if (i < 0 || i >= len) {
      err(ret, "Index out of bounds %zu (0 - %zu inclusive)", i, len - 1);
    } else {
      ret = arr.type == KT_ARRAY
                ? arr.data.array->items[i]
                : (KVal){.type = KT_NUMBER,
                         .data.number = arr.data.string.data[i]};
    }
  }
  arr_push(ctx->stack, ret);
}
void native_reverse(KCtx *ctx) {
  IN_ANY(arr);
  KVal error;
  if (arr.type == KT_STRING) {
    size_t i = 0, j = arr.data.string.len-1;
    while (i < j) {
      char tmp = arr.data.string.data[i];
      arr.data.string.data[i] = arr.data.string.data[j];
      arr.data.string.data[j] = tmp;
      i++; j--;
    }
  } else if (arr.type == KT_ARRAY) {
    size_t i = 0, j = arr.data.array->size - 1;
    while (i < j) {
      KVal tmp = arr.data.array->items[i];
      arr.data.array->items[i] = arr.data.array->items[j];
      arr.data.array->items[j] = tmp;
      i++; j--;
    }
  } else {
    err(error, "Expected string or array to reverse");
    OUT(error);
    return;
  }
  OUT(arr);
}

void native_aset(KCtx *ctx) {
  KVal val = arr_pop(ctx->stack);
  KVal idx = arr_pop(ctx->stack);
  KVal arr = arr_peek(ctx->stack);
  size_t i = (size_t)idx.data.number;
  if (i < 0 || i > arr.data.array->size) {
    KVal ret;
    err(ret, "Index out of bounds %zu (0 - %zu inclusive)", i,
        arr.data.array->size);
    arr_push(ctx->stack, ret);
  } else {
    if (i == arr.data.array->size) {
      arr_push(arr.data.array, val);
    } else {
      arr.data.array->items[i] = val;
    }
  }
}

void native_adel(KCtx *ctx) {
  KVal idx = arr_pop(ctx->stack);
  KVal arr = arr_peek(ctx->stack);
  size_t i = (size_t)idx.data.number;
  if (i < 0 || i > arr.data.array->size) {
    KVal ret;
    err(ret, "Index out of bounds %zu (0 - %zu inclusive)", i,
        arr.data.array->size - 1);
    arr_push(ctx->stack, ret);
  } else {
    for (size_t idx = i; idx < arr.data.array->size - 1; idx++) {
      arr.data.array->items[i] = arr.data.array->items[i+1];
    }
    arr.data.array->size--;
  }
}


/* (arr-in from to -- arr-in arr-out)
 * copy a slice of an array or string
 */
void native_slice(KCtx *ctx) {
  IN(to, KT_NUMBER);
  IN(from, KT_NUMBER);
  IN_ANY(arr);
  size_t len;
  KVal copy, error;
  if (arr.type == KT_STRING) {
    len = arr.data.string.len;
  } else if (arr.type == KT_ARRAY) {
    len = arr.data.array->size;
  } else {
    err(error, "Expected array or string to copy");
    goto fail;
  }
  size_t start = (size_t)from.data.number;
  size_t end = (size_t) to.data.number;
  if (start < 0 || start > len || end < 0 || end > len) {
    err(error, "Copy range (%zu - %zu) out of bounds, valid range: 0 - %zu",
        start, end, len);
    goto fail;
  }
  if (start > end) {
    err(error, "Copy start can't be after end (%zu > %zu)", start, end);
    goto fail;
  }

  if (arr.type == KT_STRING) {
    // copy string
    copy = (KVal){.type = KT_STRING,
                  .data.string = {.len = end - start,
                                  .data = tgc_alloc(&gc, end-start)}};
    memcpy(copy.data.string.data, &arr.data.string.data[start], end-start);
  } else {
    // copy array
    copy =
        (KVal){.type = KT_ARRAY, .data.array = tgc_alloc(&gc, sizeof(KArray))};
    for(size_t i=start;i<end;i++) {
      arr_push(copy.data.array, arr.data.array->items[i]);
    }
  }

  OUT(arr);
  OUT(copy);
  return;
fail:
  OUT(error);
}

bool check_ref_name(KVal ref, KVal *errv) {
 if (ref.type != KT_REF_NAME) {
   err(*errv, "Expected variable reference.");
   return false;
 }
 return true;
}

bool check_ref_value(KVal refv, KVal *errv) {
  if (refv.type != KT_REF_VALUE) {
    err(*errv, "Expected variable value.");
    return false;
  }
  return true;
}

void native_deref(KCtx *ctx) {
  KVal ref = arr_pop(ctx->stack);
  KVal val;
  if(check_ref_name(ref, &val)) {
    KVal refv = hm_get(ctx->names, ref);
    if (refv.type == KT_NIL)
      val = refv;
    else
      val = refv.data.ref->value;
  }
  arr_push(ctx->stack, val);
}

void native_reset(KCtx *ctx) {
  KVal val = arr_pop(ctx->stack);
  KVal ref = arr_pop(ctx->stack);
  KVal err;
  if (check_ref_name(ref, &err)) {
    KVal refv = hm_get(ctx->names, ref);
    if (refv.type == KT_NIL) {
      // not found, create new reference value holder
      refv = (KVal){.type = KT_REF_VALUE,
                    .data.ref = tgc_alloc(&gc, sizeof(KRef))};
      refv.data.ref->value = val;
      hm_put(ctx->names, ref, refv);
      return;
    } else {
      // already exists, just set it
      refv.data.ref->value = val;
      return;
    }
  }

  arr_push(ctx->stack, err);
}

KVal copy(KVal v) {
 switch (v.type) {
 case KT_ARRAY: {
   KArray *arr = tgc_alloc(&gc, sizeof(KArray));
   arr->capacity = v.data.array->capacity;
   arr->size = 0;
   arr->items = tgc_alloc(&gc, sizeof(KVal) * arr->capacity);
   for (size_t i = 0; i < v.data.array->size; i++) {
     arr_push(arr, copy(v));
   }
   return (KVal){.type = KT_ARRAY, .data.array = arr};
 }
 case KT_STRING: {
   KString str = {.len = v.data.string.len,
                  .data = tgc_alloc(&gc, v.data.string.len)};
   memcpy(str.data, v.data.string.data, str.len);
   return (KVal){.type = KT_STRING, .data.string = str};
 }
   // FIXME: implement hashmaps
 default:
    return v;
  }
}

void native_copy(KCtx *ctx) {
  /* make a fresh copy of item */
  IN_ANY(v);
  KVal c = copy(v);
  OUT(c);
}

void native_dump(KCtx *ctx) {
  printf("STACK(%zu): ", ctx->stack->size);
  debug_stack(ctx);
  printf("\n");
}

void native_read(KCtx *ctx) {
  char buf[512];
  char *at = buf;
  fgets(buf, 512, stdin);
  KReader in = (KReader) {.at = at};
  OUT(read(&in));
}

KNative native[] = {
    {.name = "+", .op = OP_PLUS},
    {.name = "-", .op = OP_MINUS},
    {.name = "*", .op = OP_MUL},
    {.name = "/", .op = OP_DIV},
    {.name = "<", .op = OP_LT},
    {.name = ">", .op = OP_GT},
    {.name = "<=", .op = OP_LTE},
    {.name = ">=", .op = OP_GTE},
    {.name = "%", .op = OP_MOD},
    {.name = "<<", .op = OP_SHL},
    {.name = ">>", .op = OP_SHR},
    {.name = "=", .op = OP_EQ},
    {.name = "and", .op = OP_AND},
    {.name = "or", .op = OP_OR},
    {.name = "dup", .op = OP_DUP},
    {.name = "drop", .op = OP_DROP},
    {.name = "swap", .op = OP_SWAP},
    {.name = "rot", .op = OP_ROT},
    {.name = "over", .op = OP_OVER},
    {.name = "nip", .op = OP_NIP},
    {.name = "tuck", .op = OP_TUCK},
    {.name = "move", .op = OP_MOVEN},
    {.name = "pick", .op = OP_PICKN},
    {.name = ".", .op = OP_PRINT},
    {.name = "apush", .op = OP_APUSH},
    {.name = "slurp",   .fn = native_slurp},
    {.name = "nl",      .fn = native_nl},
    {.name = "cat",     .fn = native_cat},
    {.name = "sort",    .fn = native_sort},
    {.name = "compare", .fn = native_compare},
    {.name = "len",     .fn = native_len},
    {.name = "aget",    .fn = native_aget},
    {.name = "reverse", .fn = native_reverse},
    {.name = "aset",    .fn = native_aset},
    {.name = "adel",    .fn = native_adel},
    {.name = "slice",   .fn = native_slice},
    {.name = "?",       .fn = native_deref},
    {.name = "!",       .fn = native_reset},
    {.name = "copy",    .fn = native_copy},
    {.name = "dump",    .fn = native_dump},
    {.name = "read",    .fn = native_read},

};

void (*get_native_impl(uint16_t index))(KCtx *) {
  return native[index].fn;
}

bool get_native_fn(const char *name, KNative *nat) {
  // TODO: build a trie
  for (uint16_t i = 0; i < (sizeof(native) / sizeof(KNative)); i++) {
    if (strcmp(name, native[i].name) == 0) {
      *nat = native[i];
      nat->index = i;
      return true;
    }
  }
  return false;
}


/* | | |                             | | | *
 * v v v CHECK ALL BELOW FOR REMOVAL v v v */

void native_print(KCtx *ctx) {
  kval_dump(arr_pop(ctx->stack));
}

void native_cond(KCtx *ctx) {
  KVal cond = arr_pop(ctx->stack);
  if (cond.type != KT_ARRAY || cond.data.array->size % 2) {
    KVal e;
    err(e, "Cond requires an array with alternating condition/action pairs.");
    arr_push(ctx->stack, e);
  } else {
    for (size_t i = 0; i < cond.data.array->size - 1; i++) {
      KVal _if = cond.data.array->items[i * 2 + 0];
      if(_if.type == KT_ARRAY) _if.type = KT_BLOCK;
      KVal _then = cond.data.array->items[i * 2 + 1];
      exec(ctx, _if);
      KVal result = arr_pop(ctx->stack);
      //printf("IF ");
      //kval_dump(_if);
      //printf(" THEN ");
      //kval_dump(_then);
      //printf(" ==> ");
      //kval_dump(result);
      //printf("\n");
      if (!falsy(result)) {
        // we are done, run action and return
        if(_then.type == KT_ARRAY) _then.type = KT_BLOCK;
        exec(ctx, _then);
        return;
      }
    }
  }
}

/* Copy Nth value from top and push it to top */
void native_pick(KCtx *ctx) {
  IN(num, KT_NUMBER);
  KVal error;
  size_t sz = ctx->stack->size;
  size_t idx = (size_t) num.data.number;
  if (sz <= idx) {
    err(error, "Can't pick item %zu from stack that has size %zu", idx, sz);
    OUT(error);
  } else {
    OUT(ctx->stack->items[sz - 1 - idx]);
  }
}

/* Move Nth value from top and push it to top */
void native_move(KCtx *ctx) {
  IN(num, KT_NUMBER);
  KVal error;
  size_t sz = ctx->stack->size;
  size_t idx = (size_t) num.data.number;
  if (sz <= idx) {
    err(error, "Can't move item %zu from stack that has size %zu", idx, sz);
    OUT(error);
  } else {
    KVal item = arr_remove_nth(ctx->stack, sz - 1 - idx);
    OUT(item);
  }
}

void native_dup(KCtx *ctx) {
  KVal v = arr_pop(ctx->stack);
  arr_push(ctx->stack, v);
  arr_push(ctx->stack, v);
}
void native_rot(KCtx *ctx) {
  // rotate 3rd item to top
  // (a b c -- b c a)
  IN_ANY(c);
  IN_ANY(b);
  IN_ANY(a);
  OUT(b);
  OUT(c);
  OUT(a);
}

void native_swap(KCtx *ctx) {
  KVal b = arr_pop(ctx->stack);
  KVal a = arr_pop(ctx->stack);
  arr_push(ctx->stack, b);
  arr_push(ctx->stack, a);
}
void native_drop(KCtx *ctx) { arr_pop(ctx->stack); }

void native_exec(KCtx *ctx) {
  KVal arr = arr_pop(ctx->stack);
  if (arr.type == KT_ARRAY) {
    for (size_t i = 0; i < arr.data.array->size; i++) {
      exec(ctx, arr.data.array->items[i]);
    }
  } else {
    exec(ctx, arr);
  }
}


/* Takes 2 values: an array to process and code (array or word name) to run on each item.
 * The code is invoked for each element of the 1st array with
 * the element as the top of the stack. The top of the stack after the block
 * is run, is put into the array.
 *
 * [1 2 3] [1 +] each
 * => leaves [2 3 4] on the stack
 */

void native_each(KCtx *ctx) {
  KVal code = arr_pop(ctx->stack);
  if(code.type == KT_ARRAY) code.type = KT_BLOCK;
  KVal arr = arr_pop(ctx->stack);
  KVal error;
  if(arr.type == KT_ARRAY) {
    for (size_t i = 0; i < arr.data.array->size; i++) {
      KVal item = arr.data.array->items[i];
      arr_push(ctx->stack, item);
      exec(ctx, code);
      arr.data.array->items[i] = arr_pop(ctx->stack);
    }
  } else if (arr.type == KT_STRING) {
    for (size_t i = 0; i < arr.data.string.len; i++) {
      KVal byte = (KVal){.type = KT_NUMBER,
                         .data.number = (double)arr.data.string.data[i]};
      arr_push(ctx->stack, byte);
      exec(ctx, code);
      KVal v = arr_pop(ctx->stack);
      if (v.type != KT_NUMBER) {
        err(error, "Can't store non-number value to string index: %zu", i);
        goto error;
      }
      arr.data.string.data[i] = (char) v.data.number;
    }
  } else {
    err(error, "Expected array or string to go through");
    goto error;
  }

  arr_push(ctx->stack, arr);
  return;

  error:
  arr_push(ctx->stack, error);
}

void fold(KCtx *ctx, bool init) {
  KVal code = arr_pop(ctx->stack);
  if(code.type == KT_ARRAY) code.type = KT_BLOCK;
  KVal arr = arr_pop(ctx->stack);
  KVal error;
  if(arr.type == KT_ARRAY) {
    for (size_t i = 0; i < arr.data.array->size; i++) {
      KVal item = arr.data.array->items[i];
      arr_push(ctx->stack, item);
      if(i || init)
        exec(ctx, code);
    }
  } else if (arr.type == KT_STRING) {
    for (size_t i = 0; i < arr.data.string.len; i++) {
      KVal item =
          (KVal){.type = KT_NUMBER, .data.number = arr.data.string.data[i]};
      arr_push(ctx->stack, item);
      if(i || init)
        exec(ctx, code);
    }
  } else {
    err(error, "Expected array or string to fold");
    arr_push(ctx->stack, error);
  }
}

void native_fold(KCtx *ctx) { fold(ctx, false); }
void native_foldi(KCtx *ctx) { fold(ctx, true); }

/* Run code while top of stack is truthy at the end.
 * Always runs at least 1 iteration.
 *
 * 0 [ dup . nl 1 + dup swap 3 < ] while
 * prints:
 * 0
 * 1
 * 2
 */
void native_while(KCtx *ctx) {
  IN_EXEC(loop);
  for (;;) {
    exec(ctx, loop);
    IN_ANY(condition);
    if(falsy(condition)) return;
  }
}


void native_filter(KCtx *ctx) {
 KVal code = arr_pop(ctx->stack);
 if(code.type == KT_ARRAY) code.type = KT_BLOCK;
 KVal arr = arr_pop(ctx->stack);
 KVal error;
 if (arr.type != KT_ARRAY) {
   err(error, "Expected array to filter");
   arr_push(ctx->stack, error);
   return;
 }
 size_t idx=0; // idx to put to
 for (size_t i = 0; i < arr.data.array->size; i++) {
   KVal item = arr.data.array->items[i];
   arr_push(ctx->stack, item);
   exec(ctx, code);
   KVal result = arr_pop(ctx->stack);
   if (result.type != KT_FALSE && result.type != KT_NIL) {
     arr.data.array->items[idx++] = item;
   }
 }
 // clear all residual items
 for (size_t i = idx; i < arr.data.array->size; i++) {
   arr.data.array->items[i] = (KVal){.type = KT_NIL};
 }
 arr.data.array->size = idx;
 arr_push(ctx->stack, arr);
}

void native_equals(KCtx *ctx) {
  KVal b = arr_pop(ctx->stack);
  KVal a = arr_pop(ctx->stack);
  arr_push(ctx->stack, (KVal){.type = kval_eq(a, b) ? KT_TRUE : KT_FALSE});
}

void native_not(KCtx *ctx) {
  if(falsy(arr_pop(ctx->stack))) {
    arr_push(ctx->stack, (KVal){.type = KT_TRUE});
  } else {
    arr_push(ctx->stack, (KVal){.type = KT_FALSE});
  }
}

void native_times(KCtx *ctx) {
  /* [code] N times
   * run code N times
   */
  KVal times = arr_pop(ctx->stack);
  KVal code = arr_pop(ctx->stack);
  if(code.type == KT_ARRAY) code.type = KT_BLOCK;
  long N = (long)times.data.number;
  for (long i = 0; i < N; i++) {
    exec(ctx, code);
  }
}




void native_swap_ref_value(KCtx *ctx, bool value_in_stack) {
  IN_ANY(code);
  if(code.type == KT_ARRAY) code.type = KT_BLOCK;
  IN(ref, KT_REF_NAME);
  KVal err, refv;
  if (check_ref_name(ref, &err)) {
    refv = hm_get(ctx->names, ref);
    if (refv.type == KT_NIL) {
      // not found, create new reference value holder
      refv = (KVal){.type = KT_REF_VALUE,
                    .data.ref = tgc_alloc(&gc, sizeof(KRef))};
      refv.data.ref->value = (KVal){.type = KT_NIL};
      hm_put(ctx->names, ref, refv);
    }
    // put value in stack and execute code
    OUT(refv.data.ref->value);
    exec(ctx, code);
    IN_ANY(res);
    refv.data.ref->value = res;
    if(value_in_stack) OUT(res);
    return;
  }
  OUT(err);
}

/* @foo [inc] !!
 * update value of foo by running [inc] with the current value on the top
 * doesn't leave anything on stack
 */
void native_swap_ref(KCtx *ctx) { native_swap_ref_value(ctx, false); }

/* @foo [inc] !?
 * update value of foo, like !!, but leaves new value on stack
 */
void native_swap_ref_cur(KCtx *ctx) { native_swap_ref_value(ctx, true); }

bool kokoki_eval(KCtx *ctx, const char *source);
void native_eval(KCtx *ctx) {
  IN(source, KT_STRING);
  char *src = tgc_alloc(&gc, source.data.string.len + 1);
  src[source.data.string.len] = 0;
  memcpy(src, source.data.string.data, source.data.string.len);
  kokoki_eval(ctx, src);
  tgc_free(&gc, src);
}

void native_use(KCtx *ctx) {
  native_slurp(ctx);
  native_eval(ctx);
}




/*
 * while top of the stack is true
 * @foo 0 !
 * [ "hello" . ] [ @foo get 10 > ]  while
 */

void kokoki_init(void (*callback)(KCtx*,void*), void *user) {
  int dummy;
  tgc_start(&gc, &dummy);
  KCtx *ctx = kctx_new();

  /* size_t sz; */
  /* uint8_t *stdlib; */
  /* get_resource("stdlib.ki", &sz, &stdlib); */
  /* KReader in = { */
  /*   .at = (char *)stdlib, */
  /*   .end = (char *)stdlib + sz, */
  /*   .line = 1, */
  /*   .col = 1 */
  /* }; */
  /* compile(ctx, &in, C_TOPLEVEL); */
  /* execute(ctx); */
  /* tgc_free(&gc, stdlib); */
  callback(ctx,user);
  tgc_stop(&gc);
}

bool kokoki_eval(KCtx *ctx, const char *source) {
  char *src = (char *)source;
  char *end = src + strlen(src);

  KReader in = (KReader){.at = src, .end = end, .line = 1, .col = 1};
  size_t pc = ctx->bytecode->size;
  //printf("compile more at pos: %zu\n", pc);
  compile(ctx, &in, C_TOPLEVEL);
  //printf("pc now at: %zu (size: %zu)\n", ctx->pc, ctx->bytecode->size);
  execute(ctx);

  for (size_t i = 0; i < ctx->stack->size; i++) {
    printf("%s", i == 0 ? "STACK: " : " | ");
    kval_dump(ctx->stack->items[i]);
  }
  printf("\n");
  return true;
}
