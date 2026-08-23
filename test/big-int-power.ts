function doubleString(s: string): string {
  let digits = "";
  let carry = 0;
  for (let i = s.length - 1; i >= 0; i--) {
    const ch: string = s.charAt(i);
    const d: number = ch === "0" ? 0 : ch === "1" ? 1 : ch === "2" ? 2 : ch === "3" ? 3 : ch === "4" ? 4 : ch === "5" ? 5 : ch === "6" ? 6 : ch === "7" ? 7 : ch === "8" ? 8 : 9;
    const p: number = d * 2 + carry;
    const r: number = p % 10;
    const rs: string = r === 0 ? "0" : r === 1 ? "1" : r === 2 ? "2" : r === 3 ? "3" : r === 4 ? "4" : r === 5 ? "5" : r === 6 ? "6" : r === 7 ? "7" : r === 8 ? "8" : "9";
    digits = digits + rs;
    carry = p >= 10 ? 1 : 0;
  }
  if (carry > 0) {
    digits = digits + "1";
  }
  let result = "";
  for (let j = digits.length - 1; j >= 0; j--) {
    result = result + digits.charAt(j);
  }
  return result;
}

function pow2(n: number): string {
  let result = "1";
  for (let i = 0; i < n; i++) {
    result = doubleString(result);
  }
  return result;
}

const n: number = 14000;
const answer: string = pow2(n);
console.log("Computing 2^" + n + "...");
console.log("Result has " + answer.length + " digits");
console.log(answer);
