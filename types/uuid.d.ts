/** Node.js `uuid` ambient types for mini-tsc. */
declare module "uuid" {
  function v4(): string;
  function validate(uuid: string): boolean;
  function v7(): string;

  export { v4, validate, v7 };
}
