# Kokoki, a frothy little Forth-like language

![test workflow](https://github.com/tatut/kokoki/actions/workflows/test.yml/badge.svg)

A little stack language implemented in C.

Uses [tgc](https://github.com/orangeduck/tgc) garbage collector.

# Basics

Kokoki is a concatenative stack language, loosely like Forth.

Comment start with `#` and are until the end of line.
Values push themselves on to the stack. Words operate on the stack.

Examples.

```
# define word "squared" that duplicates the top stack element and multiplies it by itself
: squared dup * ;

# use it
3 squared    # stack will now contain 9
```

Conditional execution is done with the `cond` word which operates on an array of
alternating condition, action -pairs. Cond tries the conditions in order and executes
the first action whose condition matches.

```
# cond with last fallback condition
[ [ x 10 < ] "you are a child"
  [ x 42 < ] "you are an adult"
  true       "you are quite old" ] cond

# => leaves one of the 3 strings on the stack, depending on the value of x
```
