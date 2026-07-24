#ifndef NODE_CHALK_H
#define NODE_CHALK_H

#include "runtime.h"

/* Basic color functions */
Value node_chalk_red(Value text);
Value node_chalk_green(Value text);
Value node_chalk_blue(Value text);
Value node_chalk_yellow(Value text);
Value node_chalk_magenta(Value text);
Value node_chalk_cyan(Value text);
Value node_chalk_white(Value text);
Value node_chalk_gray(Value text);
Value node_chalk_grey(Value text);
Value node_chalk_black(Value text);

/* Bright color functions */
Value node_chalk_redBright(Value text);
Value node_chalk_greenBright(Value text);
Value node_chalk_blueBright(Value text);
Value node_chalk_yellowBright(Value text);
Value node_chalk_magentaBright(Value text);
Value node_chalk_cyanBright(Value text);
Value node_chalk_whiteBright(Value text);

/* Background color functions */
Value node_chalk_bgRed(Value text);
Value node_chalk_bgGreen(Value text);
Value node_chalk_bgBlue(Value text);
Value node_chalk_bgYellow(Value text);
Value node_chalk_bgMagenta(Value text);
Value node_chalk_bgCyan(Value text);
Value node_chalk_bgWhite(Value text);
Value node_chalk_bgBlack(Value text);

/* Modifier functions */
Value node_chalk_bold(Value text);
Value node_chalk_dim(Value text);
Value node_chalk_italic(Value text);
Value node_chalk_underline(Value text);
Value node_chalk_strikethrough(Value text);
Value node_chalk_visible(Value text);
Value node_chalk_reset(Value text);

/* Extended color functions */
Value node_chalk_hex(Value color, Value text);
Value node_chalk_rgb(Value r, Value g, Value b, Value text);
Value node_chalk_ansi256(Value code, Value text);
Value node_chalk_bgHex(Value color, Value text);
Value node_chalk_bgRgb(Value r, Value g, Value b, Value text);

/* Properties */
Value node_chalk_level(void);
Value node_chalk_enabled(void);

#endif /* NODE_CHALK_H */
