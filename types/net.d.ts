/** Node.js `net` ambient types for mini-tsc. */
declare module "net" {
  interface AddressInfo {
    address: string;
    family: string;
    port: number;
  }

  interface Socket {
    readonly connecting: boolean;
    readonly destroyed: boolean;
    readonly readable: boolean;
    readonly writable: boolean;
    readonly localAddress?: string;
    readonly localPort?: number;
    readonly remoteAddress?: string;
    readonly remotePort?: number;

    write?(data: string | Buffer | Uint8Array, callback?: () => void): boolean;
    write?(data: string | Buffer | Uint8Array, encoding?: string, callback?: () => void): boolean;
    end?(callback?: () => void): void;
    end?(data: string | Buffer | Uint8Array, callback?: () => void): void;
    end?(data: string | Buffer | Uint8Array, encoding?: string, callback?: () => void): void;
    destroy?(error?: any): void;
    pause?(): Socket;
    resume?(): Socket;
    setEncoding?(encoding: string): Socket;
    setTimeout?(timeout: number, callback?: () => void): Socket;
    setNoDelay?(noDelay?: boolean): Socket;
    setKeepAlive?(enable?: boolean, initialDelay?: number): Socket;
    ref?(): Socket;
    unref?(): Socket;
    address?(): AddressInfo | string | null;

    on?(event: "connect", listener: () => void): this;
    on?(event: "data", listener: (data: Buffer | string) => void): this;
    on?(event: "end" | "close" | "error" | "timeout", listener: (...args: any[]) => void): this;
    on?(event: string, listener: (...args: any[]) => void): this;
    once?(event: "connect" | "data" | "end" | "close" | "error" | "timeout", listener: (...args: any[]) => void): this;
    once?(event: string, listener: (...args: any[]) => void): this;
    off?(event: string, listener?: (...args: any[]) => void): this;
    addListener?(event: string, listener: (...args: any[]) => void): this;
    removeListener?(event: string, listener?: (...args: any[]) => void): this;
    listeners?(event: string): Function[];
  }

  interface Server {
    readonly listening: boolean;
    maxConnections?: number;

    listen(port: number, callback?: () => void): this;
    listen(port: number, host: string, callback?: () => void): this;
    listen(options: NetListenOptions, callback?: () => void): this;
    listen(path: string, callback?: () => void): this;
    close(callback?: () => void): this;
    address(): AddressInfo | string | null;
    getConnections?(callback: (error: any, count: number) => void): void;
    ref?(): this;
    unref?(): this;

    on?(event: "listening", listener: () => void): this;
    on?(event: "connection", listener: (socket: Socket) => void): this;
    on?(event: "error" | "close" | "drop", listener: (...args: any[]) => void): this;
    on?(event: string, listener: (...args: any[]) => void): this;
    once?(event: string, listener: (...args: any[]) => void): this;
    off?(event: string, listener?: (...args: any[]) => void): this;
    addListener?(event: string, listener: (...args: any[]) => void): this;
    removeListener?(event: string, listener?: (...args: any[]) => void): this;
    listeners?(event: string): Function[];
  }

  interface NetListenOptions {
    port?: number;
    host?: string;
    path?: string;
    backlog?: number;
    exclusive?: boolean;
    ipv6Only?: boolean;
    keepAlive?: boolean;
    noDelay?: boolean;
  }

  interface NetConnectOptions {
    port?: number;
    host?: string;
    path?: string;
    family?: number;
    localAddress?: string;
    localPort?: number;
    keepAlive?: boolean;
    noDelay?: boolean;
    allowHalfOpen?: boolean;
    timeout?: number;
  }

  type ConnectionListener = (socket: Socket) => void;

  function createServer(connectionListener?: ConnectionListener): Server;
  function createServer(options: any, connectionListener?: ConnectionListener): Server;
  function createConnection(options: NetConnectOptions, connectionListener?: () => void): Socket;
  function createConnection(port: number, host?: string, connectionListener?: () => void): Socket;
  function connect(options: NetConnectOptions, connectionListener?: () => void): Socket;
  function connect(port: number, host?: string, connectionListener?: () => void): Socket;
  function isIP(input: string): number;
  function isIPv4(input: string): boolean;
  function isIPv6(input: string): boolean;

  export {
    createServer,
    createConnection,
    connect,
    isIP,
    isIPv4,
    isIPv6,
    Socket,
    Server,
    AddressInfo,
    NetListenOptions,
    NetConnectOptions,
    ConnectionListener,
  };
}
