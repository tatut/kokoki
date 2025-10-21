#ifndef kokoki_h
#define kokoki_h

#include <stdbool.h>

typedef enum KType {
  KT_NIL, // null value
  KT_TRUE, // true boolean
  KT_FALSE, // false boolean
  KT_NUMBER, // numbers double precision
  KT_STRING, // string
  KT_NAME, // variable name
  KT_ARRAY, // dynamic array of items
  KT_HASHMAP, // hashmap of values
  KT_NATIVE, // native implemented function
  KT_ERROR,  // error object (parsing or runtime)
  // syntactic tokens, not real runtime values
  KT_DEFINITION, // ':' definition (uses array where 1st item is the name)
  KT_BLOCK, // type of array that is executed in place
  KT_EOF,
} KType;

typedef struct KVal KVal;

typedef struct KArray {
  size_t size, capacity;
  KVal *items;
} KArray;

typedef struct KString {
  size_t len;
  char *data;
} KString;

typedef struct KCtx KCtx;

typedef struct KVal {
  KType type;
  union {
    double number;
    KString string;
    KArray *array;
    void (*native)(KCtx*);
  } data;
} KVal;

typedef struct KHashMapEntry {
  KVal key;
  KVal value;
  bool used;
} KHashMapEntry;

typedef struct KHashMap {
  size_t capacity;
  size_t size;
  KHashMapEntry *items;
} KHashMap;

typedef struct KCtx {
  KArray *stack;
  KHashMap *names;
} KCtx;

/**
 * Initialize system, calls given callback with the system.
 */
void kokoki_init(void (*callback)(KCtx*));

/**
 * Evaluate the given source code.
 */
void kokoki_eval(KCtx *ctx, const char *source);

/**
 * Register a native C implemented word.
 */
void kokoki_native(KCtx * ctx, const char *name, void (*native)(KCtx *));

void kval_dump(KVal v);

#endif
