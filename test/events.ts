import * as events from "events";

function main(): void {
  const ee = new events.EventEmitter();

  ee.on("data", (msg: any) => {
    console.log("data");
    console.log(msg);
  });

  ee.once("ready", () => {
    console.log("ready");
  });

  ee.emit("data", "hello");
  ee.emit("data", "world");
  ee.emit("ready");
  ee.emit("ready"); // once: should not print again

  console.log("count data");
  console.log(ee.listenerCount("data"));
  console.log("count ready");
  console.log(ee.listenerCount("ready"));

  ee.removeAllListeners("data");
  console.log("after remove data");
  console.log(ee.listenerCount("data"));
  ee.emit("data", "gone"); // no output

  ee.setMaxListeners(20);
  console.log("max");
  console.log(ee.getMaxListeners());

  console.log("defaultMax");
  console.log(events.defaultMaxListeners);

  // prependListener: later emit should call prepended first
  const ee2 = new events.EventEmitter();
  ee2.on("order", () => {
    console.log("second");
  });
  ee2.prependListener("order", () => {
    console.log("first");
  });
  ee2.emit("order");
}
main();
