/** Minimal Node.js `net` ambient types for mini-tsc. */
declare module "net" {
    interface Socket {
        write?(data: string | Buffer): boolean;
        end?(data?: string | Buffer): void;
        on?(event: string, listener: (...args: any[]) => void): this;
    }

    interface Server {
        listen?(port: number, callback?: () => void): void;
        on?(event: string, listener: (...args: any[]) => void): this;
    }

    interface NetConnectOptions {
        host?: string;
        port?: number;
    }

    type ConnectionListener = (socket: Socket) => void;

    function createServer(connectionListener?: ConnectionListener): Server;
    function createConnection(options: NetConnectOptions, connectionListener?: () => void): Socket;

    export {
        createServer,
        createConnection,
        Socket,
        Server,
        NetConnectOptions,
        ConnectionListener,
    };
}
