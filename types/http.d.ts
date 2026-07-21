/** Node.js `http` ambient types for mini-tsc (Web Request/Response handlers). */
declare module "http" {
  interface IncomingHttpHeaders {
    host?: string;
    "content-type"?: string;
    "content-length"?: string;
    [key: string]: string | string[] | undefined;
  }

  interface IncomingMessage {
    method?: string;
    url?: string;
    headers: IncomingHttpHeaders;
  }

  interface ServerResponse {
    statusCode?: number;
    write?(chunk: string | Buffer): boolean;
    end?(data?: string | Buffer): void;
    setHeader?(name: string, value: string | number): void;
  }

  interface Server {
    listen(port: number, callback?: () => void): this | void;
    listen(port: number, host: string, callback?: () => void): this | void;
    close?(callback?: () => void): void;
    on?(event: string, listener: (...args: any[]) => void): this;
  }

  /**
   * mini-tsc server handlers use Web-style Request → Response.
   * Body may be string | Buffer | Blob | WritableStream | string[] (chunked).
   */
  type RequestListener = (
    req: Request,
  ) => Response | Promise<Response> | BodyInit | Promise<BodyInit> | any;

  interface RequestOptions {
    hostname?: string;
    host?: string;
    port?: number | string;
    path?: string;
    method?: string;
    headers?: IncomingHttpHeaders | Record<string, string>;
  }

  function createServer(handler?: RequestListener): Server;
  function request(options: RequestOptions | string, callback?: (res: IncomingMessage) => void): any;
  function get(url: string | RequestOptions, callback?: (res: IncomingMessage) => void): any;

  export {
    createServer,
    request,
    get,
    Server,
    ServerResponse,
    IncomingMessage,
    IncomingHttpHeaders,
    RequestListener,
    RequestOptions,
  };
}
