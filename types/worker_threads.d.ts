/** Node.js `worker_threads` ambient types for mini-tsc. */
declare module "worker_threads" {
  type TransferListItem = ArrayBuffer | MessagePort | any;

  interface ResourceLimits {
    maxYoungGenerationSizeMb?: number;
    maxOldGenerationSizeMb?: number;
    codeRangeSizeMb?: number;
    stackSizeMb?: number;
  }

  interface WorkerOptions {
    workerData?: any;
    env?: { [key: string]: string | undefined } | typeof SHARE_ENV;
    eval?: boolean;
    stdin?: boolean;
    stdout?: boolean;
    stderr?: boolean;
    execArgv?: string[];
    resourceLimits?: ResourceLimits;
    transferList?: TransferListItem[];
    trackUnmanagedFds?: boolean;
    name?: string;
  }

  interface WorkerEventMap {
    message: [any];
    error: [any];
    exit: [number];
    online: [];
    messageerror: [any];
  }

  class MessagePort {
    on(event: "message", listener: (value: any) => void): this;
    on(event: "close", listener: () => void): this;
    on(event: "messageerror", listener: (error: any) => void): this;
    on(event: string, listener: (...args: any[]) => void): this;
    once(event: string, listener: (...args: any[]) => void): this;
    off(event: string, listener: (...args: any[]) => void): this;
    addListener(event: string, listener: (...args: any[]) => void): this;
    removeListener(event: string, listener: (...args: any[]) => void): this;
    postMessage(value: any, transferList?: TransferListItem[]): void;
    start(): void;
    close(): void;
    ref(): void;
    unref(): void;
    hasRef?(): boolean;
  }

  class MessageChannel {
    readonly port1: MessagePort;
    readonly port2: MessagePort;
  }

  class Worker {
    constructor(filename: string | URL, options?: WorkerOptions);
    readonly threadId: number;
    readonly threadName?: string;
    readonly resourceLimits?: ResourceLimits;
    readonly stdin: any;
    readonly stdout: any;
    readonly stderr: any;
    postMessage(value: any, transferList?: TransferListItem[]): void;
    terminate(): Promise<number>;
    ref(): void;
    unref(): void;
    on(event: "message", listener: (value: any) => void): this;
    on(event: "error", listener: (err: any) => void): this;
    on(event: "exit", listener: (exitCode: number) => void): this;
    on(event: "online", listener: () => void): this;
    on(event: "messageerror", listener: (error: any) => void): this;
    on(event: string, listener: (...args: any[]) => void): this;
    once(event: string, listener: (...args: any[]) => void): this;
    off(event: string, listener: (...args: any[]) => void): this;
    addListener(event: string, listener: (...args: any[]) => void): this;
    removeListener(event: string, listener: (...args: any[]) => void): this;
    getHeapSnapshot?(): any;
  }

  class BroadcastChannel {
    constructor(name: string);
    readonly name: string;
    onmessage: ((ev: { data: any }) => void) | null;
    onmessageerror: ((ev: any) => void) | null;
    postMessage(message: any): void;
    close(): void;
    ref(): void;
    unref(): void;
    on(event: "message", listener: (message: any) => void): this;
    on(event: string, listener: (...args: any[]) => void): this;
  }

  const isMainThread: boolean;
  const parentPort: MessagePort | null;
  const workerData: any;
  const threadId: number;
  const threadName: string | null;
  const isInternalThread: boolean;
  const SHARE_ENV: unique symbol;
  const resourceLimits: ResourceLimits;
  const locks: any;

  function getEnvironmentData(key: string | number | symbol): any;
  function setEnvironmentData(key: string | number | symbol, value?: any): void;
  function receiveMessageOnPort(port: MessagePort): { message: any } | undefined;
  function markAsUntransferable(object: object): void;
  function isMarkedAsUntransferable(object: object): boolean;
  function markAsUncloneable(object: object): void;
  function moveMessagePortToContext(port: MessagePort, context: any): MessagePort;
  function postMessageToThread(threadId: number, value: any, transferList?: TransferListItem[]): void;

  export {
    Worker,
    MessageChannel,
    MessagePort,
    BroadcastChannel,
    isMainThread,
    parentPort,
    workerData,
    threadId,
    threadName,
    isInternalThread,
    SHARE_ENV,
    resourceLimits,
    locks,
    getEnvironmentData,
    setEnvironmentData,
    receiveMessageOnPort,
    markAsUntransferable,
    isMarkedAsUntransferable,
    markAsUncloneable,
    moveMessagePortToContext,
    postMessageToThread,
    WorkerOptions,
    ResourceLimits,
    TransferListItem,
  };
}
