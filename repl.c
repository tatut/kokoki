#include <stdio.h>
#include "kokoki.h"


#define MAX_LINE 65536
static char line[MAX_LINE];

char *prompt(KCtx *ctx) {
  printf("\nkokoki(%zu)> ", ctx->stack->size);
  fflush(stdout);
  return fgets(line, MAX_LINE, stdin);
}

void repl(KCtx *ctx, void *user) {
  printf("Welcome to Korvatunturin Konkatenatiivinen Kieli (kokoki) REPL!\n");
  char *in;
  while((in = prompt(ctx))) {
    if(kokoki_eval(ctx, (const char *)in)) {
      printf("  ok");
    }
  }
  printf("Bye!\n");
}

void run_file(KCtx *ctx, void* file) {

  snprintf(line, MAX_LINE, "\"%s\" slurp eval", (char*) file);
  kokoki_eval(ctx, line);
}

int main(int argc, char **argv) {
  if(argc > 1) {
    kokoki_init(run_file, argv[1]);
  } else {
    kokoki_init(repl, NULL);
  }
  return 0;
}
