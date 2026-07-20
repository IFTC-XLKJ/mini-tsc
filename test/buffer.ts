// Simple Buffer test
function main(): void {
  console.log("Starting Buffer test...");

  var buf1 = Buffer.from("Hello, World!");
  console.log(buf1);
  console.log(buf1.length);
  console.log(buf1.toString());

  var buf2 = Buffer.alloc(10);
  console.log(buf2);
  console.log(buf2.length);

  var hex = buf1.toString("hex");
  console.log(hex);

  var b64 = buf1.toString("base64");
  console.log(b64);

  var sliced = buf1.slice(0, 5);
  console.log(sliced.toString());

  var buf3 = Buffer.alloc(3);
  buf3.writeUInt8(65, 0);
  buf3.writeUInt8(66, 1);
  buf3.writeUInt8(67, 2);
  console.log(buf3.toString());

  console.log(Buffer.isBuffer(buf1));
  console.log(Buffer.isBuffer("not a buffer"));

  var buf4 = Buffer.from("Hello");
  var buf5 = Buffer.from(" World");
  var combined = Buffer.concat([buf4, buf5]);
  console.log(combined.toString());

  console.log("Done!");
}
main();
