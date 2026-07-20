function main(): void {
  alert("Hello from alert");

  const ok = confirm("Continue?");
  console.log("confirm result");
  console.log(ok);

  const name = prompt("Your name:");
  console.log("prompt result");
  console.log(name);
}
main();
