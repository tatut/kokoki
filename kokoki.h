// clang-format off
#ifndef kokoki_h
#define kokoki_h

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Bytecode opcodes */
typedef enum KOp {
  OP_END = 0, // END execution, compiler puts this as last op always
  /* Push operations.
   * Put constants or fresh data structures on top of the stack.
   */
  OP_PUSH_NIL,   // the nil value
  OP_PUSH_TRUE,  // the true value
  OP_PUSH_FALSE, // the false value
  OP_PUSH_INT8,  // integer between -128 and 127, next byte is the value
  OP_PUSH_INT16, //  integer between -32768 and 32767, next 2 bytes is the value
  OP_PUSH_NUMBER, // a number, next 8 bytes is the value
  OP_PUSH_STRING, // a short string, next byte is len and the bytes after that
  OP_PUSH_STRING_LONG, // long string, next 4 bytes is len, and the bytes after
                       // that
  OP_PUSH_NAME,        // a name, like short string
  OP_PUSH_ARRAY,       // a fresh empty array
  OP_PUSH_HASHMAP,     // a fresh empty hashtable

  /* Arithmetic operations.
   * Operates on 2 topmost values on the stack
   */
  OP_PLUS,  // +
  OP_MINUS, // -
  OP_MUL,   // *
  OP_DIV,   // /
  OP_LT,    // <
  OP_GT,    // >
  OP_LTE,   // <=
  OP_GTE,   // >=
  OP_MOD,   // %
  OP_SHL,   // <<
  OP_SHR,   // >>

  OP_EQ,  // = equality
  OP_AND, // and two truth values
  OP_OR,  // or two truth values

  /* Basic stack manipulation words.
   */
  OP_DUP,  // duplicate top of stack
  OP_DROP, // drop top of stack
  OP_SWAP, // swap
  OP_ROT,  // rot
  OP_OVER, //  over
  OP_NIP,   // drop 2nd item
  OP_TUCK,  // duplicate top item and place it under 2nd item
  OP_MOVEN, // move Nth item to top
  OP_MOVE1, // move 2nd item to top (same as swap)
  OP_MOVE2, // move 3rd item to top
  OP_MOVE3, // move 4th item to top
  OP_MOVE4, // move 5th item to top
  OP_MOVE5, // move 6th item to top
  OP_PICKN, // duplicate Nth item to top
  OP_PICK1, // duplicate 2nd item to top (same as over)
  OP_PICK2, // duplicate 3rd item to top
  OP_PICK3, // duplicate 4th item to top
  OP_PICK4, // duplicate 5th item to top
  OP_PICK5, // duplicate 6th item to top

  /* Control operations.
   */
  OP_JMP,       // unconditional jump, next 3 bytes is the address
  OP_JMP_TRUE,  // conditional jump if top of stack is truthy
  OP_JMP_FALSE, // conditional jump if top of stack is falsy
  OP_CALL, // call a word (next 3 bytes is the address), pushes current pos into
           // return stack
  OP_RETURN, // return to position in top of return stack
  OP_INVOKE, // invoke a native C implemented word (2 byte index)

  /* Operations needed to arrays Inline data structure operations
   */
  OP_APUSH, // (arr item -- arr) push top of stack to array

  /* Misc */
  OP_PRINT,  // "." print value
} KOp;

typedef enum KType {
  KT_NIL,           // null value
  KT_TRUE,          // true boolean
  KT_FALSE,         // false boolean
  KT_NUMBER,        // numbers double precision
  KT_STRING,        // string
  KT_NAME,          // variable name
  KT_ARRAY_START,   // '[' start of array literal
  KT_ARRAY_END,     // ']' end of array literal
  KT_ARRAY,         // array runtime type
  KT_HASHMAP_START, // '{' start of hashmap literal
  KT_HASHMAP_END,   // '}' end of hashmap literal
  KT_HASHMAP,       // hashmap runtime type
  KT_REF_NAME,      // reference a named container for a value
  KT_REF_VALUE,     // the actual instance containing the value
  KT_NATIVE,        // native implemented function
  KT_ERROR,         // error object (parsing or runtime)
  KT_DEF_START,     // ':' definition (uses array where 1st item is the name)
  KT_DEF_END,       // ';' ends definition
  KT_BLOCK,         // type of array that is executed in place
  KT_EOF,           // end of input
  KT_CODE_ADDR,     // byte code address (for compiled word definition)
  KT_COMMA          // ',' delimiter for arrays and hashmaps

} KType;

/* Struct for reader */
typedef struct KReader {
  char *at;
  char *end;
  int line, col;
  KType last_token_type;
} KReader;



// push to dynamic array, growing it as needed
#define ARR_PUSH(arr, v)                                                       \
  do {                                                                         \
    if ((arr)->size == (arr)->capacity) {                                      \
      size_t new_capacity = (arr)->capacity == 0 ? 8 : (arr)->capacity * 1.62; \
      (arr)->items = tgc_realloc(&gc, (arr)->items,                            \
                                 new_capacity * sizeof((arr)->items[0]));      \
      (arr)->capacity = new_capacity;                                          \
      if (!(arr)->items) {                                                     \
        fprintf(stderr, "Out of memory at: %s line %d\n", __FILE__, __LINE__); \
        exit(1);                                                               \
      }                                                                        \
    }                                                                          \
    (arr)->items[(arr)->size++] = v;                                           \
  } while (false)

#define ARR_REMOVE_FIRST(arr)                                                  \
  do {                                                                         \
    if ((arr)->size == 1) {                                                    \
      (arr)->size = 0;                                                         \
    } else {                                                                   \
      for (size_t _i = 0; _i < (arr)->size - 1; _i++) {                        \
        (arr)->items[_i] = (arr)->items[_i + 1];                               \
      }                                                                        \
      (arr)->size -= 1;                                                        \
    }                                                                          \
  } while (false)



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
    uint32_t address;
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

typedef struct KByteCode {
  size_t capacity;
  size_t size;
  uint8_t *items;
} KByteCode;

typedef struct KAddrStack {
  size_t capacity;
  size_t size;
  uint32_t *items;
} KAddrStack;

typedef struct KCtx {
  KArray *stack;
  KHashMap *names;

  // bytecode
  KByteCode *bytecode;

  // current program counter
  uint32_t pc;

  // return address from any calls
  KAddrStack *return_addr;
} KCtx;

/**
 * Initialize system, calls given callback with the system.
 */
void kokoki_init(void (*callback)(KCtx*,void*), void* user);

/**
 * Compile and run the given source code.
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

void emit(KCtx *ctx, KOp op);
void emit_bytes(KCtx *ctx, size_t len, uint8_t *bytes);
void execute(KCtx *ctx);

#endif
