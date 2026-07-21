import * as fs from "fs";

async function main(): Promise<void> {
  console.log("=== Concurrent async FS ===");

  await fs.writeFile("c1.txt", "AAA");
  await fs.writeFile("c2.txt", "BBB");

  // Kick off both reads without awaiting between starts
  const p1 = fs.readFile("c1.txt", "utf-8");
  const p2 = fs.readFile("c2.txt", "utf-8");

  const a = await p1;
  const b = await p2;
  console.log("c1:", a);
  console.log("c2:", b);

  await fs.unlink("c1.txt");
  await fs.unlink("c2.txt");
  console.log("=== concurrent ok ===");
}

main();
