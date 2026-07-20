/** Minimal Node.js `http` ambient types for mini-tsc. */
declare module "http" {
  interface IncomingHttpHeaders {
    host?: string;
    [key: string]: string | string[] | undefined;
  }

  interface IncomingMessage {
    method?: string;
    url?: string;
    headers: IncomingHttpHeaders;
  }

  interface Server {
    listen(port: number, callback?: () => void): void;
  }

  /**
   * mini-tsc server handlers use Web-style Request/Response.
   * Body may be string | Buffer | Blob | etc. (runtime accepts Buffer).
   */
  type RequestListener = (req: Request) => Response | Promise<Response> | any;

  function createServer(handler?: RequestListener): Server;
  function request(options: any, callback?: any): any;
  function get(url: any, callback?: any): any;

  export {
    createServer, request, get, Server, IncomingMessage, IncomingHttpHeaders, RequestListener,
  };
}

