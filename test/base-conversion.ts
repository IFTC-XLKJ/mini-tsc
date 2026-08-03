// Test cases for convertBase API
console.log("=== Base Conversion Tests ===");

// Integer conversions
console.log("Decimal to Binary:");
console.log(convertBase("10", 10, 2));      // "1010"
console.log(convertBase("255", 10, 2));     // "11111111"

console.log("\nHex to Decimal:");
console.log(convertBase("FF", 16, 10));     // "255"
console.log(convertBase("1A", 16, 10));     // "26"

console.log("\nDecimal to Hex:");
console.log(convertBase("255", 10, 16));    // "FF"
console.log(convertBase("26", 10, 16));     // "1A"

console.log("\nBinary to Octal:");
console.log(convertBase("1010", 2, 8));     // "12"
console.log(convertBase("11111111", 2, 8)); // "377"

// Fractional conversions
console.log("\nFractional Tests:");
console.log(convertBase("3.14", 10, 2));    // Binary with fraction
console.log(convertBase("1010.11", 2, 16)); // Binary to hex with fraction
console.log(convertBase("A.C", 16, 10));    // Hex to decimal with fraction

// Edge cases
console.log("\nEdge Cases:");
console.log(convertBase("0", 10, 2));       // "0"
console.log(convertBase("-10", 10, 2));     // "-1010"
console.log(convertBase("+FF", 16, 10));    // "255"

// Base 36
console.log("\nBase 36:");
console.log(convertBase("ZZ", 36, 10));     // 36*35+35 = 1295
console.log(convertBase("1295", 10, 36));   // "ZZ"
