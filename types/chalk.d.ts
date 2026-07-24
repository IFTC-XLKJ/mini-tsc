declare module "chalk" {
  type Level = 0 | 1 | 2 | 3;

  interface ChalkInstance {
    (text: any): string;
    red: ChalkInstance;
    green: ChalkInstance;
    blue: ChalkInstance;
    yellow: ChalkInstance;
    magenta: ChalkInstance;
    cyan: ChalkInstance;
    white: ChalkInstance;
    gray: ChalkInstance;
    grey: ChalkInstance;
    black: ChalkInstance;
    redBright: ChalkInstance;
    greenBright: ChalkInstance;
    blueBright: ChalkInstance;
    yellowBright: ChalkInstance;
    magentaBright: ChalkInstance;
    cyanBright: ChalkInstance;
    whiteBright: ChalkInstance;
    bgRed: ChalkInstance;
    bgGreen: ChalkInstance;
    bgBlue: ChalkInstance;
    bgYellow: ChalkInstance;
    bgMagenta: ChalkInstance;
    bgCyan: ChalkInstance;
    bgWhite: ChalkInstance;
    bgBlack: ChalkInstance;
    bold: ChalkInstance;
    dim: ChalkInstance;
    italic: ChalkInstance;
    underline: ChalkInstance;
    strikethrough: ChalkInstance;
    visible: ChalkInstance;
    reset: ChalkInstance;
    level: Level;
    enabled: boolean;
    hex(color: string, text?: string): ChalkInstance;
    rgb(r: number, g: number, b: number, text?: string): ChalkInstance;
    ansi256(code: number, text?: string): ChalkInstance;
    bgHex(color: string, text?: string): ChalkInstance;
    bgRgb(r: number, g: number, b: number, text?: string): ChalkInstance;
  }

  const chalk: ChalkInstance;
  export default chalk;
  export { chalk, Level, ChalkInstance };
}
