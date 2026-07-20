/** Minimal Node.js `crypto` ambient types for mini-tsc. */
declare module "crypto" {
  interface Hash {
    update(data: string | Buffer): Hash;
    digest(encoding?: string): string;
  }

  interface Hmac {
    update(data: string | Buffer): Hmac;
    digest(encoding?: string): string;
  }

  function randomBytes(size: number): string;
  function randomUUID(): string;
  function createHash(algorithm: string): Hash;
  function createHmac(algorithm: string, key: string): Hmac;
  function pbkdf2Sync(
    password: string,
    salt: string,
    iterations: number,
    keylen: number,
    digest?: string,
  ): string;
  function pbkdf2(
    password: string,
    salt: string,
    iterations: number,
    keylen: number,
    digest: string,
    callback: (err: any, derivedKey: string) => void,
  ): void;
  function md5(data: string): string;
  function sha1(data: string): string;
  function sha256(data: string): string;
  function sha512(data: string): string;
  function hmac_sha256(key: string, data: string): string;
  function scryptSync(password: string, salt: string, keylen: number): string;

  export {
    randomBytes, randomUUID, createHash, createHmac, pbkdf2Sync, pbkdf2,
    md5, sha1, sha256, sha512, hmac_sha256, scryptSync, Hash, Hmac,
  };
}
