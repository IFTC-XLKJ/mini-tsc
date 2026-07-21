/** Node.js `readline` ambient types for mini-tsc. */
declare module "readline" {
  interface ReadLineOptions {
    input?: any;
    output?: any;
    prompt?: string;
    terminal?: boolean;
  }

  interface Interface {
    question(query: string, callback: (answer: string) => void): void;
    close(): void;
    on(event: "line", listener: (input: string) => void): this;
    on(event: "close", listener: () => void): this;
    on(event: string, listener: (...args: any[]) => void): this;
    prompt(preserveCursor?: boolean): void;
    setPrompt(prompt: string): void;
    getPrompt(): string;
    write(data: string): void;
    pause(): this;
    resume(): this;
  }

  function createInterface(options?: ReadLineOptions): Interface;
  function clearLine(stream: any, dir?: number): void;
  function cursorTo(stream: any, x: number, y?: number): void;
  function moveCursor(stream: any, dx: number, dy: number): void;

  export {
    createInterface,
    clearLine,
    cursorTo,
    moveCursor,
    Interface,
    ReadLineOptions,
  };
}
