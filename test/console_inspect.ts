function main(): void {
  // Simple values
  console.log(42);
  console.log("hello");
  console.log(true);
  console.log(null);

  // Simple array
  console.log([1, 2, 3]);

  // Nested array
  console.log([1, [2, 3], 4]);

  // Simple object
  console.log({ name: "Alice", age: 30 });

  // Nested object
  console.log({ user: { name: "Bob", scores: [90, 85, 95] }, active: true });

  // Array of objects
  console.log([{ id: 1, name: "a" }, { id: 2, name: "b" }]);

  // Object with array values
  console.log({ tags: ["ts", "c", "transpiler"], count: 3 });

  // Mixed complex
  const data = {
    title: "Test",
    items: [
      { x: 1, y: 2 },
      { x: 3, y: 4 }
    ],
    meta: { version: "1.0", debug: false }
  };
  console.log(data);

  // info/warn/error with objects
  console.info({ level: "info", msg: "blue object" });
  console.warn({ level: "warn", msg: "yellow object" });
  console.error({ level: "error", msg: "red object" });

  console.log("done");
}
main();
