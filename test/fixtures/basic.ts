function greet(name: string): string {
  return "Hello, " + name + "!";
}

function add(a: number, b: number): number {
  return a + b;
}

function main(): void {
  for (let i = 0; i < 10; i++) {
    console.log(i);
  }
  const msg = greet("World");
  console.log(msg);
  console.log(typeof msg);

  const result = add(1, 2);
  console.log(result);
}

main();
