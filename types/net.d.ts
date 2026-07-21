/** Node.js `net` ambient types for mini-tsc. */
declare module "net" {
  interface Socket {
    write?(data: string | Buffer | Uint8Array): boolean;
    end?(data?: string | Buffer | Uint8Array): void;
    destroy?(): void;
    on?(event: "data", listener: (data: Buffer | string) => void): this;
    on?(event: "end" | "close" | "error" | "connect", listener: (...args: any[]) => void): this;
    on?(event: string, listener: (...args: any[]) => void): this;
  }

  interface Server {
    listen?(port: number, callback?: () => void): this | void;
    listen?(port: number, host: string, callback?: () => void): this | void;
    close?(callback?: () => void): void;
    on?(event: "connection", listener: (socket: Socket) => void): this;
    on?(event: "error" | "listening" | "close", listener: (...args: any[]) => void): this;
    on?(event: string, listener: (...args: any[]) => void): this;
  }

  interface NetConnectOptions {
    host?: string;
    port?: number;
    path?: string;
  }

  type ConnectionListener = (socket: Socket) => void;

  function createServer(connectionListener?: ConnectionListener): Server;
  function createServer(options: any, connectionListener?: ConnectionListener): Server;
  function createConnection(options: NetConnectOptions, connectionListener?: () => void): Socket;
  function createConnection(port: number, host?: string, connectionListener?: () => void): Socket;
  function connect(options: NetConnectOptions, connectionListener?: () => void): Socket;

  export {
    createServer,
    createConnection,
    connect,
    Socket,
    Server,
    NetConnectOptions,
    ConnectionListener,
  };
}
