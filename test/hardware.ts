import { manufacturer, model, serial, biosVersion, biosReleaseDate, cpus, arch, totalmem } from "os";

console.log("=== Hardware Information ===");
console.log("Manufacturer:", manufacturer());
console.log("Model:", model());
console.log("Serial:", serial());
console.log("BIOS Version:", biosVersion());
console.log("BIOS Release Date:", biosReleaseDate());
console.log("");
console.log("=== Additional System Info ===");
console.log("Architecture:", arch());
console.log("CPU:", cpus()[0]?.model || "unknown");
console.log("Total Memory:", Math.round(totalmem() / 1024 / 1024 / 1024 * 10) / 10, "GB");
