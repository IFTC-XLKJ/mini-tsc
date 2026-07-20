/** Minimal Node.js `path` ambient types for mini-tsc. */
declare module "path" {
    interface ParsedPath {
        root: string;
        dir: string;
        base: string;
        ext: string;
        name: string;
    }

    function join(...paths: string[]): string;
    function resolve(...paths: string[]): string;
    function basename(path: string, ext?: string): string;
    function dirname(path: string): string;
    function extname(path: string): string;
    function normalize(path: string): string;
    function parse(path: string): ParsedPath;
    function format(pathObject: ParsedPath): string;
    function isAbsolute(path: string): boolean;
    function relative(from: string, to: string): string;

    export {
        join,
        resolve,
        basename,
        dirname,
        extname,
        normalize,
        parse,
        format,
        isAbsolute,
        relative,
        ParsedPath,
    };
}
