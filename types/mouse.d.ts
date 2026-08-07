/** Global mouse event monitoring for mini-tsc (Win32). */
declare module "mouse" {
  type MouseButton = "left" | "right" | "middle";

  interface MouseEvent {
    x: number;
    y: number;
    button: MouseButton;
    pressed: boolean;
    delta: number;
  }

  interface MousePosition {
    x: number;
    y: number;
  }

  /** Start the global mouse hook. Must be called before on(). */
  function start(): boolean;

  /** Stop the global mouse hook and remove all listeners. */
  function stop(): boolean;

  /** Register a callback for a mouse event. */
  function on(event: "mousemove", listener: (x: number, y: number) => void): void;
  function on(event: "mousedown", listener: (button: MouseButton, pressed: boolean, x: number, y: number) => void): void;
  function on(event: "mouseup", listener: (button: MouseButton, pressed: boolean, x: number, y: number) => void): void;
  function on(event: "click", listener: (button: MouseButton, pressed: boolean, x: number, y: number) => void): void;
  function on(event: "wheel", listener: (delta: number) => void): void;

  /** Register a one-time callback. */
  function once(event: string, listener: (...args: any[]) => void): void;

  /** Remove a previously registered callback. */
  function off(event: string, listener: (...args: any[]) => void): void;

  /** Get the current cursor position (screen coordinates). */
  function getPosition(): MousePosition;

  /** Check if a specific button is currently pressed. */
  function isButtonDown(button: MouseButton): boolean;

  /** Get the number of registered listeners for an event. */
  function listenerCount(event: string): number;

  export { start, stop, on, once, off, getPosition, isButtonDown, listenerCount };
}
