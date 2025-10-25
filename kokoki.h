#ifndef kokoki_h
#define kokoki_h

#include <stdbool.h>
#include <stddef.h>

typedef enum KType {
  KT_NIL,        // null value
  KT_TRUE,       // true boolean
  KT_FALSE,      // false boolean
  KT_NUMBER,     // numbers double precision
  KT_STRING,     // string
  KT_NAME,       // variable name
  KT_ARRAY,      // dynamic array of items
  KT_HASHMAP,    // hashmap of values
  KT_REF_NAME,   // reference a named container for a value
  KT_REF_VALUE,  // the actual instance containing the value
  KT_NATIVE,     // native implemented function
  KT_ERROR,      // error object (parsing or runtime)
  KT_DEFINITION, // ':' definition (uses array where 1st item is the name)
  KT_BLOCK,      // type of array that is executed in place
  KT_EOF,        // end of input
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
typedef struct KRef KRef;

typedef struct KVal {
  KType type;
  union {
    double number;
    KString string;
    KArray *array;
    void (*native)(KCtx*);
    KRef *ref;
  } data;
} KVal;

typedef struct KRef {
  KVal value;
} KRef;

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
void kokoki_init(void (*callback)(KCtx*,void*), void* user);

/**
 * Evaluate the given source code.
 * Returns true on success, false otherwise.
 */
bool kokoki_eval(KCtx *ctx, const char *source);

/**
 * Register a native C implemented word.
 */
void kokoki_native(KCtx * ctx, const char *name, void (*native)(KCtx *));

void kval_dump(KVal v);

void arr_push(KArray *arr, KVal val);
KVal arr_pop(KArray *arr);

#endif
