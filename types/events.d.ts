/** Minimal Node.js `events` ambient types for mini-tsc. */
declare module "events" {
  type Listener = (...args: any[]) => void;

  class EventEmitter {
    constructor();
    on(event: string | symbol, listener: Listener): this;
    addListener(event: string | symbol, listener: Listener): this;
    once(event: string | symbol, listener: Listener): this;
    off(event: string | symbol, listener: Listener): this;
    removeListener(event: string | symbol, listener: Listener): this;
    prependListener(event: string | symbol, listener: Listener): this;
    prependOnceListener(event: string | symbol, listener: Listener): this;
    emit(event: string | symbol, ...args: any[]): boolean;
    removeAllListeners(event?: string | symbol): this;
    listenerCount(event: string | symbol): number;
    listeners(event: string | symbol): Listener[];
    rawListeners(event: string | symbol): Listener[];
    eventNames(): Array<string | symbol>;
    setMaxListeners(n: number): this;
    getMaxListeners(): number;
  }

  function getEventListeners(emitter: EventEmitter, event: string | symbol): Listener[];
  let defaultMaxListeners: number;

  export { EventEmitter, getEventListeners, defaultMaxListeners };
  export default EventEmitter;
}
