#ifndef NODE_CHALK_H
#define NODE_CHALK_H

#include "runtime.h"

/* Chalk instance is a TAG_OBJECT Value wrapping ChalkCtx*. */

/* Default singleton (like npm chalk default export). */
Value node_chalk_default(void);

/* Apply styles: chalk(...) / builder(...) */
Value node_chalk_apply(Value self, Value* args, int argc);

/* Style property getters: chalk.red / chalk.bold → new builder */
Value node_chalk_style(Value self, Value styleName);

/* Color model methods: chalk.rgb(r,g,b) / chalk.hex("#ff0000") */
Value node_chalk_rgb(Value self, Value r, Value g, Value b);
Value node_chalk_bgRgb(Value self, Value r, Value g, Value b);
Value node_chalk_hex(Value self, Value color);
Value node_chalk_bgHex(Value self, Value color);
Value node_chalk_ansi(Value self, Value code);
Value node_chalk_bgAnsi(Value self, Value code);
Value node_chalk_ansi256(Value self, Value index);
Value node_chalk_bgAnsi256(Value self, Value index);
Value node_chalk_keyword(Value self, Value name);
Value node_chalk_bgKeyword(Value self, Value name);
Value node_chalk_hsl(Value self, Value h, Value s, Value l);
Value node_chalk_bgHsl(Value self, Value h, Value s, Value l);
Value node_chalk_hsv(Value self, Value h, Value s, Value v);
Value node_chalk_bgHsv(Value self, Value h, Value s, Value v);
Value node_chalk_hwb(Value self, Value h, Value w, Value b);
Value node_chalk_bgHwb(Value self, Value h, Value w, Value b);

/* Instance / level / supportsColor */
Value node_chalk_Instance(Value options);
Value node_chalk_level(Value self);
Value node_chalk_setLevel(Value self, Value level);
Value node_chalk_supportsColor(void);
Value node_chalk_stderr(void);

#endif /* NODE_CHALK_H */
