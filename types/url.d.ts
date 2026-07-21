/** Node.js-style `url` module ambient types for mini-tsc. */
declare module "url" {
  interface URLSearchParams {
    get(name: string): string | null;
    set(name: string, value: string): void;
    toString?(): string;
  }

  interface URL {
    href: string;
    protocol: string;
    host: string;
    hostname: string;
    port: string;
    pathname: string;
    search: string;
    hash: string;
    origin?: string;
    searchParams: URLSearchParams;
    toString(): string;
  }

  function fileURLToPath(url: URL | string): string;
  function pathToFileURL(path: string): URL;

  export { URL, URLSearchParams, fileURLToPath, pathToFileURL };
}
