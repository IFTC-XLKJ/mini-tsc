import * as mouse from "mouse";

function main(): void {
  console.log("Mouse module test");

  // Start listening for mouse events
  mouse.start();
  console.log("Mouse hook started");

  // Get current position
  const pos = mouse.getPosition();
  console.log("Initial position:");
  console.log(pos.x);
  console.log(pos.y);

  // Listen to all mouse events
  mouse.on("any", (event: any) => {
    console.log("Event:");
    console.log(event.eventType);
    console.log(event.x);
    console.log(event.y);
    console.log(event.button);
  });

  // Listen to specific events
  mouse.on("move", (event: any) => {
    console.log("Move:");
    console.log(event.x);
    console.log(event.y);
  });

  mouse.on("left_click", (event: any) => {
    console.log("Left click at:");
    console.log(event.x);
    console.log(event.y);
  });

  mouse.on("right_click", (event: any) => {
    console.log("Right click at:");
    console.log(event.x);
    console.log(event.y);
  });

  mouse.on("scroll", (event: any) => {
    console.log("Scroll delta:");
    console.log(event.delta);
  });

  console.log("Listening for events... Press Ctrl+C to stop.");

  // Stop after 5 seconds
  setTimeout(() => {
    mouse.stop();
    console.log("Mouse hook stopped");
  }, 5000);
}

main();
