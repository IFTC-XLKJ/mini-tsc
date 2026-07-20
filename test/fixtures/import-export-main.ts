import { add, multiply, PI } from "./import-export";

function main(): void {
  const sum = add(3, 4);
  console.log(sum);

  const product = multiply(5, 6);
  console.log(product);

  console.log(PI);
}

main();
