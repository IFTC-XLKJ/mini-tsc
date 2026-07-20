import * as fs from "fs";
import * as path from "path";

function main(): void {
  // Test path operations
  const joined = path.join("foo", "bar", "baz.txt");
  console.log(joined);

  const ext = path.extname("file.txt");
  console.log(ext);

  // Test fs operations
  const exists = fs.existsSync("package.json");
  console.log(exists);

  if (exists) {
    const content = fs.readFileSync("package.json", "utf-8");
    console.log(content);
  }
}

main();
