import { gpuInfo } from "os";

console.log("=== GPU Information ===");
const gpus: any = gpuInfo();
console.log("Found " + gpus.length + " GPU(s)");

for (let i = 0; i < gpus.length; i++) {
  const gpu: any = gpus[i];
  console.log("");
  console.log("GPU " + i + ":");
  console.log("  Name: " + gpu.name);
  console.log("  Vendor: " + gpu.vendor);
  console.log("  Memory: " + gpu.memoryMB + " MB");
  console.log("  Driver: " + gpu.driverVersion);
  console.log("  Utilization: " + gpu.utilization + "%");
  console.log("  Temperature: " + gpu.temperature + " C");
}
