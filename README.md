# mini-tsc

https://iftc.koyeb.app/mini-tsc

A simple TypeScript compiler. It can compile TypeScript code to Executable files.

Supported Platform: Windows/Linux(include Termux)

Not Supported Platform: MacOS

## Usage

```bash
npm install
npm run build
node dist/cli/index.js compile <input_ts_file> -o <output_exec_file>
```

## Example

```bash
node dist/cli/index.js compile test/all.ts -o all
```

## License

[MIT](LICENSE)
