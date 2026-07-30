import { platform, defaultEncoding, EOL, devNull } from "os";

console.log("Platform:", platform);
console.log("Default Encoding:", defaultEncoding);
console.log("EOL:", JSON.stringify(EOL));
console.log("Dev Null:", devNull);
