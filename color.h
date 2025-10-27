typedef enum ANSI_COLOR {
  UNSET=0,
  BLACK,
  RED,
  GREEN,
  YELLOW,
  BLUE,
  PURPLE,
  CYAN,
  WHITE
} ANSI_COLOR;

const char *color_names[] = {
  [UNSET] = "UNSET",
  [BLACK] = "BLACK",
  [RED] = "RED",
  [GREEN] = "GREEN",
  [YELLOW] = "YELLOW",
  [BLUE] = "BLUE",
  [PURPLE] = "PURPLE",
  [CYAN] = "CYAN",
  [WHITE] = "WHITE"
};

const char *colors[] = {
    [UNSET] = "",          [BLACK] = "\e[0;30m",  [RED] = "\e[0;31m",
    [GREEN] = "\e[0;32m",  [YELLOW] = "\e[0;33m", [BLUE] = "\e[0;34m",
    [PURPLE] = "\e[0;35m", [CYAN] = "\e[0;36m",   [WHITE] = "\e[0;37m"};


const char *background[] = {
    [UNSET] = "",        [BLACK] = "\e[40m",  [RED] = "\e[41m",
    [GREEN] = "\e[42m",  [YELLOW] = "\e[43m", [BLUE] = "\e[44m",
    [PURPLE] = "\e[45m", [CYAN] = "\e[46m",   [WHITE] = "\e[47m"};


const char *_reset = "\e[0m";

void col(ANSI_COLOR c) { printf("%s", colors[c]); }
void bg(ANSI_COLOR c) { printf("%s", background[c]); }
void reset() { printf("%s", _reset); }
