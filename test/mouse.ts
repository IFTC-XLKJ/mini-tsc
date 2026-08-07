import * as mouse from "mouse";

function main(): void {
  mouse.start();
  console.log("Mouse hook started. Move your mouse or click...");

  const pos = mouse.getPosition();
  console.log("Current position:", pos.x, pos.y);

  const leftDown = mouse.isButtonDown("left");
  console.log("Left button down:", leftDown);

  mouse.on("mousemove", (x: number, y: number) => {
    console.log("Move:", x, y);
  });

  mouse.on("mousedown", (button: string, pressed: boolean, x: number, y: number) => {
    console.log("Down:", button, pressed, x, y);
  });

  mouse.on("mouseup", (button: string, pressed: boolean, x: number, y: number) => {
    console.log("Up:", button, pressed, x, y);
  });

  mouse.on("wheel", (delta: number) => {
    console.log("Wheel:", delta);
  });

  setTimeout(() => {
    const count = mouse.listenerCount("mousemove");
    console.log("mousemove listeners:", count);
    mouse.stop();
    console.log("Mouse hook stopped.");
  }, 5000);
}

main();
