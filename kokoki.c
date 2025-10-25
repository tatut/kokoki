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
    } else if (c == '(') {
      while (c != ')') {
        next(at);
        c = **at;
      }
      next(at);
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

KVal copy_str(KType type, char *start, char *end) {
  size_t len = (size_t)(end - start);
  KVal s = (KVal){.type = type,
                  .data.string = {.len = len, .data = tgc_alloc(&gc, len)}};
  memcpy(s.data.string.data, start, len);
  return s;
}

KVal read_str(char **at) {
  char *start = *at + 1;
  char *end = start;
  while (*end != '"') end++;
  *at = end + 1;
  return copy_str(KT_STRING, start, end);
}

KVal read_name(char **at) {
  char *start = *at;
  char *end = start;
  while (is_name_char(*end)) end++;
  *at = end;
  return copy_str(KT_NAME, start, end);
}

KVal read_ref(char **at) {
  char *start = *at + 1;
  char *end = start;
  while (is_name_char(*end))
    end++;
  *at = end;
  return copy_str(KT_REF_NAME, start, end);
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
  //printf("arr->size before push: %zu\n", arr->size);
  arr->items[arr->size++] = v;
  //printf("arr->size after push: %zu\n", arr->size);
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
  case 0:
    return (KVal){.type = KT_EOF};
  case '@':
    return read_ref(at);
  case '"':
    return read_str(at);
  case '0': case '1': case '2': case '3': case '4': case '5':
  case '6': case '7': case '8': case '9':
    return read_num(at);
  case '-':
    // read the number or name depending on the next char
    if (is_digit(*(*at + 1)))
      return read_num(at);
    else
      return read_name(at);

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
    if ((v.data.number - (long)v.data.number) == 0.0) {
      printf("%ld", (long)v.data.number);
    } else {
      printf("%f", v.data.number);
    }
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

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)

#define IN(name, typename)                                                     \
  KVal name = arr_pop(ctx->stack);                                             \
  if (name.type != typename) {                                                 \
    KVal errv;                                                                 \
    err(errv, "Expected type " STRINGIFY(type));                               \
    arr_push(ctx->stack, errv);                                                \
    return;                                                                    \
  }


#define IN_ANY(name) KVal name = arr_pop(ctx->stack)
#define OUT(v) arr_push(ctx->stack, (v))


void native_mod(KCtx *ctx) {
  KVal b = arr_pop(ctx->stack);
  KVal a = arr_pop(ctx->stack);
  arr_push(ctx->stack, (KVal){
      .type = KT_NUMBER,
      .data.number = (double)((long)a.data.number %
                              (long)b.data.number)
    });
}

void native_print(KCtx *ctx) {
  kval_dump(arr_pop(ctx->stack));
}

void native_nl(KCtx *ctx) { printf("\n"); }

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
      if (result.type == KT_TRUE) {
        // we are done, run action and return
        if(_then.type == KT_ARRAY) _then.type = KT_BLOCK;
        exec(ctx, _then);
        return;
      }
    }
  }
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

void native_fold(KCtx *ctx) {
  KVal code = arr_pop(ctx->stack);
  if(code.type == KT_ARRAY) code.type = KT_BLOCK;
  KVal arr = arr_pop(ctx->stack);
  KVal error;
  if(arr.type == KT_ARRAY) {
    for (size_t i = 0; i < arr.data.array->size; i++) {
      KVal item = arr.data.array->items[i];
      arr_push(ctx->stack, item);
      if(i)
        exec(ctx, code);
    }
  } else if (arr.type == KT_STRING) {
    for (size_t i = 0; i < arr.data.string.len; i++) {
      KVal item =
          (KVal){.type = KT_NUMBER, .data.number = arr.data.string.data[i]};
      arr_push(ctx->stack, item);
      if(i)
        exec(ctx, code);
    }
  } else {
    err(error, "Expected array or string to fold");
    arr_push(ctx->stack, error);
  }
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

bool falsy(KVal v) { return (v.type == KT_FALSE || v.type == KT_NIL); }

void native_not(KCtx *ctx) {
  if(falsy(arr_pop(ctx->stack))) {
    arr_push(ctx->stack, (KVal){.type = KT_TRUE});
  } else {
    arr_push(ctx->stack, (KVal){.type = KT_FALSE});
  }
}

void native_and(KCtx *ctx) {
  IN_ANY(b);
  IN_ANY(a);
  OUT((KVal){.type = (falsy(a) || falsy(b)) ? KT_FALSE : KT_TRUE});
}

void native_apush(KCtx *ctx) {
  IN_ANY(v);
  IN(arr, KT_ARRAY);
  arr_push(arr.data.array, v);
  arr_push(ctx->stack, arr);
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

void native_alen(KCtx *ctx) {
  KVal arr = arr_peek(ctx->stack);
  arr_push(ctx->stack, (KVal) {.type = KT_NUMBER, .data.number = arr.data.array->size});
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

void kokoki_eval(KCtx *ctx, const char *source);
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

/*
 * while top of the stack is true
 * @foo 0 !
 * [ "hello" . ] [ @foo get 10 > ]  while
 */

void kokoki_init(void (*callback)(KCtx*,void*), void *user) {
  int dummy;
  tgc_start(&gc, &dummy);
  KCtx *ctx = kctx_new();
#define DO(name, op, type) native(ctx, STRINGIFY(op), native_##name);
  DO_NUM_OPS
#undef DO
  native(ctx, "=", native_equals);
  native(ctx, "%", native_mod);
  native(ctx, "dup", native_dup);
  native(ctx, "swap", native_swap);
  native(ctx, "drop", native_drop);
  native(ctx, "exec", native_exec);
  native(ctx, "cond", native_cond);
  native(ctx, ".", native_print);
  native(ctx, "nl", native_nl);
  native(ctx, "slurp", native_slurp);
  native(ctx, "each", native_each);
  native(ctx, "fold", native_fold);
  native(ctx, "cat", native_cat);
  native(ctx, "filter", native_filter);
  native(ctx, "not", native_not);
  native(ctx, "and", native_and);
  native(ctx, "apush", native_apush);
  native(ctx, "alen", native_alen);
  native(ctx, "aget", native_aget);
  native(ctx, "aset", native_aset);
  native(ctx, "adel", native_adel);
  native(ctx, "times", native_times);
  native(ctx, "?", native_deref);
  native(ctx, "!", native_reset);
  native(ctx, "!!", native_swap_ref);
  native(ctx, "!?", native_swap_ref_cur);
  native(ctx, "eval", native_eval);
  native(ctx, "use", native_use);
  native(ctx, "reverse", native_reverse);
  native(ctx, "copy", native_copy);
  callback(ctx,user);
  tgc_stop(&gc);
}

void kokoki_eval(KCtx *ctx, const char *source) {
  char *src = (char*) source;
  char **at = &src;
  KVal kv = read(at);

  while (kv.type != KT_EOF) {
    exec(ctx, kv);
    kv = read(at);
  }
}
