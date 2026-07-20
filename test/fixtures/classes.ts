class Point {
  x: number;
  y: number;

  constructor(x: number, y: number) {
    this.x = x;
    this.y = y;
  }

  distance(): number {
    return Math.sqrt(this.x * this.x + this.y * this.y);
  }

  toString(): string {
    return "Point(" + this.x + ", " + this.y + ")";
  }
}

function main(): void {
  const p = new Point(3, 4);
  console.log(p.toString());
  console.log(p.distance());
}

main();
