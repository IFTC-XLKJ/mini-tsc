import { parentPort, workerData } from "worker_threads";

console.log("Worker started with data:", workerData);

parentPort?.on("message", (msg: any) => {
  console.log("Worker received:", msg);
  
  // Send response back
  parentPort?.postMessage({ type: "pong", data: msg.data });
  
  // If receive ping, respond with pong
  if (msg.type === "ping") {
    parentPort?.postMessage({ type: "response", data: "pong from worker" });
  }
});

// Signal worker is ready
parentPort?.postMessage({ type: "ready", threadId: require("worker_threads").threadId });
