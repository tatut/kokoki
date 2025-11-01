# Bytecode design

Initial version of kokoki was just a reader and an evaluator.
All words were looked up all the time.

Next version is compilation to bytecode before execution.
The bytecode will not have a separate constant pool. All constants
are included in place (no deduplication).

Words will be compiled to just a jump address (index in bytecode).
A RET instruction will be added to the end.


Implement the following bytecodes:
- END (0, exit back from evaluation)
- push constants: PUSH_NIL, PUSH_TRUE, PUSH_FALSE, PUSH_NUM, PUSH_STR, PUSH_NAME
- push a new array: PUSH_ARRAY (pushes a fresh array to top of stack
- math: `+` `-` `/` `*` `%`
- basic stack manipulation: DUP, DROP, SWAP, ROT
- pick: PICKN (pick based on stack value), PICK1 ... PICK7
- move: MOVEN, MOVE1 ... MOVE7
- JMP unconditional jump to 3byte address (max size 16mb)
- JT conditional jump if top of stack is truthy
- JF conditional jump if top of stack is falsy
- CALL jump to defined word address (pushing current pos into return stack)
- RET return from call
- INVOKE standard C native function (2byte idx, 65536 max functions)


Word definitions are registered as they come, so anything used later must
be defined beforehand. Calls to words just compile to CALL instructions
with an address.
