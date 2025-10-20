/* kokoki = Korvatunturin Konkatenatiivinen Kieli
 * A Forth like programming language.
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "tgc/tgc.h"
#include "kokoki.h"

static tgc_t gc;

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

  default: return 0;// ERROR, EOF, KT_DEFINIION don't have hash
  }

}

#define VA_ARGS(...) , ##__VA_ARGS__
#define err(val, fmt, ...)                                                     \
  {                                                                            \
    (val).type = KT_ERROR;                                                     \
    size_t _errlen = (size_t)snprintf(NULL, 0, fmt VA_ARGS(__VA_ARGS__));      \
    (val).data.string.len = _errlen;                                           \
    (val).data.string.data = tgc_alloc(&gc, _errlen - 1);                      \
    snprintf((val).data.string.data, _errlen, fmt VA_ARGS(__VA_ARGS__));       \
  }




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
    if (a.data.string.len != b.data.string.len)
      return false;
    return memcmp(a.data.string.data, b.data.string.data, a.data.string.len)==0;
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
    size_t new_capacity = hm->capacity == 0 ? 32 : hm->capacity * 1.62;
    // we need to rehash
    KHashMapEntry *new_items = tgc_calloc(&gc, sizeof(KHashMapEntry), new_capacity);
    if (!new_items) {
      fprintf(stderr, "Out of memory for hashmap\n");
      exit(1);
    }
    for (size_t i = 0; i < hm->size; i++) {
      for (idx = hash % new_capacity; (new_items[idx].used && !kval_eq(key, new_items[idx].key));
           idx = (idx + 1) % new_capacity)
        ;
      new_items[idx] = (KHashMapEntry) {.key = key, .value = value, .used = true };
    }
    if(hm->items) free(hm->items);
    hm->items = new_items;
    hm->capacity = new_capacity;
  }
  for (idx = hash % hm->capacity; (hm->items[idx].used && !kval_eq(key, hm->items[idx].key));
       idx = (idx + 1) % hm->capacity)
    ;
  hm->items[idx] = (KHashMapEntry){.key = key, .value = value, .used = true};
}



KVal hm_get(KHashMap *hm, KVal key) {
  uint32_t hash = kval_hash(key);
  size_t idx = hash % hm->capacity;
  size_t orig_idx;
  while (hm->items[idx].used) {
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
  return ctx;
}

#define next(at) *(at) = *(at) + 1;

/* Parsing */
void skipws(char **at) {
 start: {
    char c = **at;
    while(c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      next(at);
      c = **at;
    }
    if (c == '#') {
      while (c != '\n') {
        next(at);
        c = **at;
      }
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
         ch == '>' || ch == '=' || ch == '?' || ch == '.' || ch == '*';
}

bool is_name_char(char ch) {
  return is_name_start_char(ch) || is_digit(ch) || ch == '-';
}

bool looking_at(char *at, char *word) {
  size_t c = 0;
  while(*word != 0) {
    if(*at == 0) return false;
    if(*at != *word) return false;
    at++;
    word++;
    c++;
  }
  return true;
}

KVal read_str(char **at) {
  char *start = *at + 1;
  char *end = start;
  while (*end != '"') end++;
  *at = end + 1;
  return (KVal){.type = KT_STRING,
                .data.string = {.len = end - start, .data = start}};
}
KVal read_name(char **at) {
  char *start = *at;
  char *end = start;
  while (is_name_char(*end)) end++;
  *at = end;
  return (KVal){.type = KT_NAME,
                .data.string = {.len = end - start, .data = start}};

}
KVal read_num(char **at) {
  double mult = 1.0;
  double val = 0;
  if (**at == '-') {
    mult = -1.0;
    *at = *at + 1;
  }
  while (is_digit(**at)) {
    val = 10 * val + (**at - 48);
    *at = *at + 1;
  }
  if (**at == '.') {
    // has fraction
    *at = *at + 1;
    double frac = 0, div = 1;
    while (is_digit(**at)) {
      frac = 10 * frac + (**at - 48);
      div *= 10;
      *at = *at + 1;
    }
    val += (frac / div);
  }
  return (KVal){.type = KT_NUMBER, .data.number = val};
}

KVal read(char **at);

void arr_push(KArray *arr, KVal v) {
  if (arr->size == arr->capacity) {
    size_t new_capacity = arr->capacity == 0 ? 8 : arr->capacity * 1.62;
    arr->items = tgc_realloc(&gc, arr->items, new_capacity * sizeof(KVal));
    arr->capacity = new_capacity;
    if (!arr->items) {
      fprintf(stderr, "Out of memory");
      exit(1);
    }
  }
  arr->items[arr->size++] = v;
}

void arr_remove_first(KArray *arr) {
  if (arr->size == 1) {
    arr->size = 0;
  } else {
    for (int i = 0; i < arr->size - 1; i++) {
      arr->items[i] = arr->items[i+1];
    }
    arr->size -= 1;
  }
}

const char *ERR_STACK_UNDERFLOW = "Stack underflow!";
KVal arr_pop(KArray *arr) {
  if(arr->size == 0) {
    return (KVal){
      .type = KT_ERROR,
      .data.string={
        .len = strlen(ERR_STACK_UNDERFLOW),
        .data = (char*)ERR_STACK_UNDERFLOW}};
  }
  return arr->items[--arr->size];
}




KVal read_array_(char **at, char endch) {
  KArray *arr = tgc_calloc(&gc, 1, sizeof(KArray));
  *at = *at + 1;
  while (**at != endch) {
    KVal v = read(at);
    arr_push(arr, v);
    skipws(at);
  }
  *at = *at + 1;
  return (KVal){.type = KT_ARRAY, .data.array = arr};
}

KVal read_array(char **at) { return read_array_(at, ']'); }

KVal read_definition(char **at) {
  KVal def = read_array_(at, ';');
  def.type = KT_DEFINITION;
  if (def.data.array->size < 2) {
    err(def, "Expected name and at least one token in definition");
  } else if (def.data.array->items[0].type != KT_NAME) {
    err(def, "Defnition must start with a name to define");
  }
  return def;
}


KVal read(char **at) {
  skipws(at);
  switch (**at) {
  case 0: return (KVal){.type=KT_EOF};
  case '"':
    return read_str(at);
  case '0': case '1': case '2': case '3': case '4': case '5':
  case '6': case '7': case '8': case '9': case '-':
    return read_num(at);
  case 't': {
    if (looking_at(*at, "true")) {
      *at = *at + 4;
      return (KVal){.type = KT_TRUE};
    }
    return read_name(at);
  }
  case 'f': {
    if (looking_at(*at, "false")) {
      *at = *at + 5;
      return (KVal){.type = KT_FALSE};
    }
    return read_name(at);
  }
  case 'n': {
    if (looking_at(*at, "nil")) {
      *at = *at + 3;
      return (KVal){.type = KT_NIL};
    }
    return read_name(at);
  }
  case ':':
    return read_definition(at);
  case '[':
    return read_array(at);
  default:
    if (is_name_start_char(**at)) {
      return read_name(at);
    }
  }
  int err_len = snprintf(NULL, 0, "Parse error at: '%c'     ", **at);
  *at = *at + 1;
  char *err = tgc_alloc(&gc, err_len);
  snprintf(err, err_len, "Parse error at: '%c'", **at);
  return (KVal){.type=KT_ERROR, .data.string = {.len=err_len-1, err}};
}

void kval_dump(KVal v) {
  switch (v.type) {
  case KT_NIL:
    printf("nil");
    break;
  case KT_TRUE:
    printf("true");
    break;
  case KT_FALSE:
    printf("false");
    break;
  case KT_STRING:
    printf("\"%.*s\"", (int) v.data.string.len, v.data.string.data);
    break;
  case KT_NAME:
    printf("%.*s", (int)v.data.string.len, v.data.string.data);
    break;
  case KT_NUMBER:
    printf("%f", v.data.number);
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

  case KT_DEFINITION:
  case KT_BLOCK:
    printf(v.type == KT_BLOCK ? "{" : ": ");
    for (size_t i = 0; i < v.data.array->size; i++) {
      if (i > 0)
        printf(" ");
      kval_dump(v.data.array->items[i]);
    }
    printf(v.type == KT_BLOCK ? "}" : " ; ");
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
  }
}

void exec(KCtx *ctx, KVal v) {
  //printf("EXECUTING %d: ", v.type);
  //kval_dump(v);
  //printf(" STACK:");
  //for (size_t i = 0; i < ctx->stack->size; i++) {
  //  printf(" ");
  //  kval_dump(ctx->stack->items[i]);
  //}
  //printf("\n");
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
    arr_push(ctx->stack, v);
    break;

  case KT_DEFINITION: {
    KVal name = v.data.array->items[0];
    arr_remove_first(v.data.array);
    v.type = KT_BLOCK;
    hm_put(ctx->names, name, v);
    //printf("defined name: ");
    //kval_dump(name);
    //printf("\n");
    break;
  }

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

void native(KCtx * ctx, const char *name, void (*native)(KCtx *)) {
  size_t len = strlen(name);
  hm_put(ctx->names,
         (KVal){.type = KT_NAME, .data.string = {.len = len, .data = (char*)name}},
         (KVal){.type = KT_NATIVE, .data.native = native});
}
void kokoki_native(KCtx *ctx, const char *name, void (*callback)(KCtx *)) {
  native(ctx, name, callback);
}

#define num_op(name, op, type)                                                 \
  void native_##name(KCtx *ctx) {                                              \
    KVal b = arr_pop(ctx->stack);                                              \
    KVal a = arr_pop(ctx->stack);                                              \
    arr_push(ctx->stack, type(op));                                            \
  }

#define NUM(op)                                                                \
  (KVal) { .type = KT_NUMBER, .data.number = a.data.number op b.data.number }

#define BOOL(op)                                                             \
    (KVal) { .type = (a.data.number op b.data.number) ? KT_TRUE : KT_FALSE }

#define DO_NUM_OPS                                                             \
  DO(plus, +, NUM)                                                             \
  DO(minus, -, NUM)                                                            \
  DO(mult, *, NUM)                                                             \
  DO(div, /, NUM)                                                              \
  DO(lt, <, BOOL)                                                              \
  DO(lte, <=, BOOL)                                                            \
  DO(gt, >, BOOL)                                                              \
  DO(gte, >=, BOOL)

#define DO(name, op, type) num_op(name, op, type)
DO_NUM_OPS
#undef DO

void native_print(KCtx *ctx) {
  kval_dump(arr_pop(ctx->stack));
}

void native_cond(KCtx *ctx) {
  /*
    [ [condition block] [then block ]]
    eg.
    [ [ x 10 < ] [ "you are a child" .]
      [ x 42 < ] [ "you are an adult" .]
      true [ "you are quite old" .] ] cond

*/
  //  KVal
}

void native_dup(KCtx *ctx) {
  KVal v = arr_pop(ctx->stack);
  arr_push(ctx->stack, v);
  arr_push(ctx->stack, v);
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

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)

void kokoki_init(void (*callback)(KCtx*)) {
  int dummy;
  tgc_start(&gc, &dummy);
  KCtx *ctx = kctx_new();
#define DO(name, op, type) native(ctx, STRINGIFY(op), native_##name);
  DO_NUM_OPS
#undef DO
  native(ctx, "dup", native_dup);
  native(ctx, "swap", native_swap);
  native(ctx, "drop", native_drop);
  native(ctx, "exec", native_exec);

  native(ctx, ".", native_print);

  callback(ctx);
  tgc_stop(&gc);
}

void kokoki_eval(KCtx *ctx, const char *source) {
  char *src = (char*) source;
  char **at = &src;
  KVal kv = read(at);

  while(kv.type != KT_EOF) {
    exec(ctx, kv);
    kv = read(at);
  }
}
