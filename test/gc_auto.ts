/**
 * Memory auto-optimization smoke test:
 * - allocate many temporary strings
 * - call explicit gc()
 * - must exit 0
 */

function makeTemps(n: number): string {
  let last = "";
  for (let i = 0; i < n; i++) {
    const s = "item-" + i + "-xxxxxxxxxxxxxxxx";
    const t = s + "-tail";
    last = t;
  }
  return last;
}

function main(): void {
  const result = makeTemps(8000);
  console.log("last:", result);

  // Explicit collection
  gc();

  const result2 = makeTemps(3000);
  console.log("last2:", result2);

  console.log("gc_auto ok");
}

main();
