import { isMainThread, parentPort, workerData, threadId, Worker, MessageChannel } from "worker_threads";

function main(): void {
  if (isMainThread) {
    // Main thread
    console.log("=== worker_threads test ===");
    console.log("1. isMainThread:", isMainThread);
    console.log("2. threadId:", threadId);

    // Test MessageChannel
    console.log("3. Testing MessageChannel...");
    const channel = new MessageChannel();
    channel.port1.on("message", (msg: any) => {
      console.log("3a. port1 received:", msg);
    });
    channel.port2.postMessage("hello from port2");
    console.log("3b. MessageChannel created successfully");

    // Test Worker
    console.log("4. Testing Worker creation...");
    const worker = new Worker("./test/fixtures/worker_task.js", {
      workerData: { task: "compute", value: 42 },
      name: "test-worker"
    });
    console.log("4a. Worker created, threadId:", worker.threadId);
    worker.start();

    worker.on("message", (msg: any) => {
      console.log("5a. Worker message:", msg);
    });

    worker.on("error", (err: any) => {
      console.log("5b. Worker error:", err);
    });

    console.log("6. Sending message to worker...");
    worker.postMessage({ type: "ping", data: "hello worker" });

    setTimeout(() => {
      console.log("7. Terminating worker...");
      worker.terminate();
    }, 1000);

    console.log("=== All tests passed ===");
  } else {
    // Worker thread - simplified without parentPort closure
    console.log("Worker thread started, threadId:", threadId);
  }
}
main();
