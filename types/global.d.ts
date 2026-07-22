/**
 * Global ambient types for mini-tsc.
 * Covers Node-like globals (process, Buffer, timers) and Web APIs used by
 * the runtime (fetch, Response, streams, URL, Blob, console, Math, …).
 */

/* ==================== Shared aliases ==================== */

type BufferEncoding =
    | "ascii"
    | "utf8"
    | "utf-8"
    | "utf16le"
    | "ucs2"
    | "ucs-2"
    | "base64"
    | "latin1"
    | "binary"
    | "hex";

type TimerHandle = number;

/* ==================== Buffer ==================== */

/* Structural Buffer (not extending Uint8Array — avoids ArrayBufferLike conflicts). */
interface Buffer {
    readonly length: number;
    readonly byteLength?: number;
    [index: number]: number;
    toString(encoding?: BufferEncoding): string;
    slice(start?: number, end?: number): Buffer;
    subarray?(start?: number, end?: number): Buffer;
    readUInt8(offset?: number): number;
    writeUInt8(value: number, offset?: number): number;
}

interface BufferConstructor {
    from(
        data: string | number[] | Buffer | ArrayBuffer | Uint8Array,
        encoding?: BufferEncoding,
    ): Buffer;
    alloc(size: number, fill?: string | number | Buffer, encoding?: BufferEncoding): Buffer;
    allocUnsafe(size: number): Buffer;
    concat(list: Buffer[], totalLength?: number): Buffer;
    isBuffer(obj: any): obj is Buffer;
    new (size: number): Buffer;
}

declare const Buffer: BufferConstructor;

/* ==================== process ==================== */

interface ProcessStream {
    readonly fd: number;
    readonly isTTY: boolean;
    readonly rows: number;
    readonly columns: number;
    write(data: string | Buffer | Uint8Array): boolean;
    on(event: "data", listener: (chunk: string | Buffer) => void): this;
    on(event: string, listener: (...args: any[]) => void): this;
    cursorTo(x: number, y?: number): boolean;
    moveCursor(dx: number, dy: number): boolean;
    clearScreenDown(): boolean;
    clearLine(dir?: number): boolean;
}

interface ProcessEnv {
    [key: string]: string | undefined;
}

interface Process {
    readonly env: ProcessEnv;
    readonly argv: string[];
    readonly pid: number;
    readonly platform: string;
    readonly stdin: ProcessStream;
    readonly stdout: ProcessStream;
    readonly stderr: ProcessStream;
    cwd(): string;
    chdir(directory: string): void;
    exit(code?: number): never;
    on(event: "message", listener: (message: any) => void): this;
    on(event: string, listener: (...args: any[]) => void): this;
    send(message: any): boolean;
}

declare var process: Process;

/* ==================== Module path globals ==================== */

declare const __dirname: string;
declare const __filename: string;

/* ==================== Timers ==================== */

declare function setTimeout(
    callback: (...args: any[]) => void,
    delay?: number,
    ...args: any[]
): TimerHandle;
declare function setInterval(
    callback: (...args: any[]) => void,
    delay?: number,
    ...args: any[]
): TimerHandle;
declare function clearTimeout(id?: TimerHandle): void;
declare function clearInterval(id?: TimerHandle): void;

/* ==================== Dialogs (stdin/stdout) ==================== */

/** Print message and wait for Enter. */
declare function alert(message?: any): void;
/** Print message + "[y/N]"; loops until y/Y (true) or n/N (false). */
declare function confirm(message?: any): boolean;
/** Print message, read a line; empty input returns null. */
declare function prompt(message?: any): string | null;

/* ==================== console ==================== */

interface Console {
    log(...data: any[]): void;
    info(...data: any[]): void;
    warn(...data: any[]): void;
    error(...data: any[]): void;
    debug(...data: any[]): void;
    time(label?: string): void;
    timeEnd(label?: string): void;
    table(tabularData?: any): void;
    trace(...data: any[]): void;
    group?(...data: any[]): void;
    groupEnd?(): void;
}

declare var console: Console;

/* Math / Date / Error / Promise come from TypeScript ES lib — not redeclared. */

/* JSON extras used by mini-tsc runtime / tests (beyond ES lib) */
interface JSON {
  isRawJSON(value: any): boolean;
  rawJSON(text: string): any;
}

/* ESM import.meta (NodeNext / bundlers) */
interface ImportMeta {
  url: string;
  dirname?: string;
  filename?: string;
  resolve?(specifier: string): string;
}

/* ==================== URL / URLSearchParams ==================== */

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

interface URLConstructor {
    new (url: string, base?: string | URL): URL;
}

declare var URL: URLConstructor;
declare var URLSearchParams: {
    new (init?: string | Record<string, string>): URLSearchParams;
};

/* ==================== Headers / Request / Response / fetch ==================== */

type HeadersInit = Headers | Record<string, string> | Array<[string, string]>;

interface Headers {
    get(name: string): string | null;
    set(name: string, value: string): void;
    has?(name: string): boolean;
    delete?(name: string): void;
    append?(name: string, value: string): void;
}

interface HeadersConstructor {
    new (init?: HeadersInit): Headers;
}

declare var Headers: HeadersConstructor;

type BodyInit =
    | string
    | Buffer
    | Blob
    | Uint8Array
    | ArrayBuffer
    | ReadableStream
    | WritableStream
    | WritableStreamDefaultWriter<any>
    | WebSocketServer
    | null;

type RequestMode = "cors" | "no-cors" | "same-origin" | "navigate" | string;
type RequestCredentials = "omit" | "same-origin" | "include" | string;
type RequestCache = "default" | "no-store" | "reload" | "no-cache" | "force-cache" | "only-if-cached" | string;
type RequestRedirect = "follow" | "error" | "manual" | string;

interface RequestInit {
    method?: string;
    headers?: HeadersInit;
    body?: BodyInit | null;
    mode?: RequestMode;
    credentials?: RequestCredentials;
    cache?: RequestCache;
    redirect?: RequestRedirect;
    referrer?: string;
    referrerPolicy?: string;
    integrity?: string;
    keepalive?: boolean;
    signal?: AbortSignal | null;
    /** mini-tsc / undici extras accepted at runtime */
    [key: string]: any;
}

interface Request {
    readonly url: string;
    readonly method: string;
    readonly headers: Headers;
    readonly body?: ReadableStream | null;
}

interface RequestConstructor {
    new (input: string | Request, init?: RequestInit): Request;
}

declare var Request: RequestConstructor;

interface ResponseInit {
    status?: number;
    statusText?: string;
    headers?: HeadersInit;
}

interface Response {
    readonly status: number;
    readonly statusText: string;
    readonly headers: Headers;
    readonly url: string;
    readonly body: ReadableStream | null;
    readonly ok?: boolean;
    text(): Promise<string>;
    json(): Promise<any>;
    blob(): Promise<Blob>;
    arrayBuffer?(): Promise<ArrayBuffer>;
    clone(): Response;
}

interface ResponseConstructor {
    new (
        body?: BodyInit | null,
        init?: ResponseInit,
    ): Response;
}

declare const Response: ResponseConstructor;

declare function fetch(input: string | Request | URL, init?: RequestInit): Promise<Response>;

/* ==================== Blob ==================== */

interface BlobPropertyBag {
    type?: string;
}

interface Blob {
    readonly size: number;
    readonly type: string;
    text?(): Promise<string>;
    arrayBuffer(): Promise<ArrayBuffer>;
}

interface BlobConstructor {
    new (
        blobParts?: Array<string | Buffer | Blob | ArrayBuffer | Uint8Array>,
        options?: BlobPropertyBag,
    ): Blob;
}

declare var Blob: BlobConstructor;

/* ==================== Streams (mini-tsc subset) ==================== */

interface ReadableStreamDefaultReader<R = any> {
    read(): Promise<{ done: boolean; value: R }>;
    releaseLock(): void;
    cancel(reason?: any): Promise<void>;
}

interface ReadableStream<R = any> {
    getReader(): ReadableStreamDefaultReader<R>;
    locked?: boolean;
    cancel(reason?: any): Promise<void>;
}

interface WritableStreamDefaultWriter<W = any> {
    write(chunk?: W): void | Promise<void>;
    close(): void | Promise<void>;
    abort(reason?: any): void | Promise<void>;
    releaseLock(): void;
}

interface WritableStream<W = any> {
    getWriter(): WritableStreamDefaultWriter<W>;
    locked?: boolean;
    abort(reason?: any): Promise<void>;
    close(): Promise<void>;
}

interface TransformStream<I = any, O = any> {
    readonly readable: ReadableStream<O>;
    readonly writable: WritableStream<I>;
}

interface ReadableStreamConstructor {
    new <R = any>(underlyingSource?: any, strategy?: any): ReadableStream<R>;
}

interface WritableStreamConstructor {
    new <W = any>(underlyingSink?: any, strategy?: any): WritableStream<W>;
}

interface TransformStreamConstructor {
    new <I = any, O = any>(
        transformer?: any,
        writableStrategy?: any,
        readableStrategy?: any,
    ): TransformStream<I, O>;
}

declare var ReadableStream: ReadableStreamConstructor;
declare var WritableStream: WritableStreamConstructor;
declare var TransformStream: TransformStreamConstructor;

/* ==================== TextEncoder / TextDecoder ==================== */

interface TextEncoder {
    encode(input?: string): Uint8Array;
}

interface TextDecoder {
    decode(input?: ArrayBuffer | Uint8Array | Buffer): string;
}

declare var TextEncoder: { new (): TextEncoder };
declare var TextDecoder: { new (label?: string): TextDecoder };

/* ==================== AbortController / AbortSignal ==================== */

interface AbortSignal {
    readonly aborted: boolean;
    onabort?: ((this: AbortSignal, ev: any) => any) | null;
    addEventListener?(type: string, listener: (...args: any[]) => void): void;
    removeEventListener?(type: string, listener: (...args: any[]) => void): void;
}

interface AbortController {
    readonly signal: AbortSignal;
    abort(reason?: any): void;
}

declare var AbortController: { new (): AbortController };
declare var AbortSignal: {
    new (): AbortSignal;
    abort?(reason?: any): AbortSignal;
};

/* ==================== Event / EventTarget (minimal) ==================== */

interface Event {
    readonly type: string;
    readonly target?: any;
    preventDefault?(): void;
    stopPropagation?(): void;
}

interface EventTarget {
    addEventListener(type: string, listener: (...args: any[]) => void): void;
    removeEventListener(type: string, listener: (...args: any[]) => void): void;
    dispatchEvent?(event: Event): boolean;
}

declare var Event: { new (type: string, eventInitDict?: any): Event };
declare var EventTarget: { new (): EventTarget };

/* ==================== Crypto (global stubs) ==================== */

interface CryptoKey {}
interface SubtleCrypto {
    digest?(algorithm: any, data: ArrayBuffer | Uint8Array): Promise<ArrayBuffer>;
}

interface Crypto {
    readonly subtle?: SubtleCrypto;
    getRandomValues?<T extends ArrayBufferView>(array: T): T;
    randomUUID?(): string;
}

declare var Crypto: { new (): Crypto };
declare var CryptoKey: { new (): CryptoKey };
declare var SubtleCrypto: { new (): SubtleCrypto };
declare var crypto: Crypto;

/* ==================== GC (optional) ==================== */

/** Explicit GC when runtime exposes it (test / debug). */
declare function gc(): void;

interface WebSocketEvent extends Event {
    data?: string | ArrayBuffer;
}

interface WebSocket {
    onopen?: (ev: WebSocketEvent) => any;
    onmessage?: (ev: WebSocketEvent) => any;
    onerror?: (ev: WebSocketEvent) => any;
    onclose?: (ev: WebSocketEvent) => any;
    addEventListener(type: string, listener: (ev: WebSocketEvent) => void): void;
    removeEventListener(type: string, listener: (ev: WebSocketEvent) => void): void;
    close(code?: number, reason?: string): void;
    send(data: string | ArrayBuffer): void;
    readonly readyState: number;
    static readonly CONNECTING: number;
    static readonly OPEN: number;
    static readonly CLOSING: number;
    static readonly CLOSED: number;
}

declare var WebSocket: { new (url: string): WebSocket };
declare var WebSocketEvent: { new (): WebSocketEvent };

interface WebSocketServer {
    onmessage?: (ev: WebSocketEvent) => any;
    onclose?: (ev: WebSocketEvent) => any;
    onerror?: (ev: WebSocketEvent) => any;
    addEventListener(type: string, listener: (ev: WebSocketEvent) => void): void;
    removeEventListener(type: string, listener: (ev: WebSocketEvent) => void): void;
    close(code?: number, reason?: string): void;
    send(data: string | ArrayBuffer): void;
    readonly readyState: number;
    static readonly CONNECTING: number;
    static readonly OPEN: number;
    static readonly CLOSING: number;
    static readonly CLOSED: number;
}

declare var WebSocketServer: { new (): WebSocketServer };