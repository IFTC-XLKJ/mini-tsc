import chalk from "chalk";

function main(): void {
  // Basic color functions
  console.log(chalk.red("Hello, red world!"));
  console.log(chalk.green("Hello, green world!"));

  // Chained styles
  console.log(chalk.red.bold("Red bold!"));
  console.log(chalk.green.underline("Green underlined!"));

  // Hex and RGB colors (two-argument form)
  console.log(chalk.hex("#FF8800", "Orange text!"));
  console.log(chalk.rgb(100, 200, 50, "Custom RGB color!"));

  // Background colors
  console.log(chalk.bgRed("White on red background!"));
  console.log(chalk.bgBlue("Text on blue background!"));

  // Modifiers
  console.log(chalk.dim("Dimmed text"));
  console.log(chalk.italic("Italic text"));
  console.log(chalk.strikethrough("Strikethrough text"));

  // Bright colors
  console.log(chalk.cyanBright("Bright cyan!"));
  console.log(chalk.redBright("Bright red!"));

  // Properties
  console.log(chalk.level);
  console.log(chalk.enabled);
}

main();
