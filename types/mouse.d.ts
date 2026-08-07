/** Node.js `mouse` ambient types for mini-tsc. */
declare module "mouse" {
  interface MouseEvent {
    /** X coordinate */
    x: number;
    /** Y coordinate */
    y: number;
    /** Button: 0=left, 1=right, 2=middle, -1=none(move/scroll) */
    button: number;
    /** Event type: "move", "left_click", "left_press", "left_release",
     *  "right_click", "right_press", "right_release",
     *  "middle_click", "middle_press", "middle_release", "scroll" */
    eventType: string;
    /** Scroll delta: positive=up, negative=down (only for scroll events) */
    delta: number;
  }

  /** Register a listener for mouse events. Event types: "move", "click", "press", "release", "scroll", "left_click", "left_press", "left_release", "right_click", "right_press", "right_release", "middle_click", "middle_press", "middle_release", "any" */
  function on(event: string, callback: (event: MouseEvent) => void): void;

  /** Remove a previously registered listener */
  function off(event: string, callback: (event: MouseEvent) => void): void;

  /** Start listening for mouse events (installs a global mouse hook) */
  function start(): void;

  /** Stop listening for mouse events (removes the global mouse hook) */
  function stop(): void;

  /** Get the current mouse position */
  function getPosition(): { x: number; y: number };

  export { MouseEvent, on, off, start, stop, getPosition };
}
