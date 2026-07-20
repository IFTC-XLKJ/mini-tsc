import * as readline from "readline";

function main(): void {
  const rl = readline.createInterface({});

  rl.setPrompt("name> ");
  console.log(rl.getPrompt());

  rl.question("What is your name? ", (answer: string) => {
    console.log("hello");
    console.log(answer);
    rl.close();
  });

  rl.on("close", () => {
    console.log("closed");
  });
}
main();
