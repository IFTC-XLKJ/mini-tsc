/** Node.js `events` ambient types for mini-tsc. */
declare module "events" {
  type Listener = (...args: any[]) => void;
  type EventName = string | symbol;

  class EventEmitter {
    constructor();
    on(event: EventName, listener: Listener): this;
    addListener(event: EventName, listener: Listener): this;
    once(event: EventName, listener: Listener): this;
    off(event: EventName, listener: Listener): this;
    removeListener(event: EventName, listener: Listener): this;
    prependListener(event: EventName, listener: Listener): this;
    prependOnceListener(event: EventName, listener: Listener): this;
    emit(event: EventName, ...args: any[]): boolean;
    removeAllListeners(event?: EventName): this;
    listenerCount(event: EventName): number;
    listeners(event: EventName): Listener[];
    rawListeners(event: EventName): Listener[];
    eventNames(): EventName[];
    setMaxListeners(n: number): this;
    getMaxListeners(): number;
  }

  function getEventListeners(emitter: EventEmitter, event: EventName): Listener[];
  let defaultMaxListeners: number;

  export { EventEmitter, getEventListeners, defaultMaxListeners, Listener, EventName };
  export default EventEmitter;
}
