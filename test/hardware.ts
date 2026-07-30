import { networkStats } from "os";

console.log("=== Network Traffic Statistics ===");
const stats: any = networkStats();
console.log("Found " + stats.length + " active interface(s)");

for (let i = 0; i < stats.length; i++) {
  const stat: any = stats[i];
  console.log("");
  console.log(stat.name);
  if (stat.description) console.log("  Description: " + stat.description);
  if (stat.mac) console.log("  MAC: " + stat.mac);
  console.log("  Received:      " + stat.bytesReceived + " bytes (" + stat.packetsReceived + " packets)");
  console.log("  Sent:          " + stat.bytesSent + " bytes (" + stat.packetsSent + " packets)");
  console.log("  Errors (in):   " + stat.errorsReceived);
  console.log("  Errors (out):  " + stat.errorsSent);
}
