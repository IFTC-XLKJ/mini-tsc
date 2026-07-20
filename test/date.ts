function main(): void {
    // Date.now() returns a number (timestamp)
    const now: number = Date.now();
    console.log("Now:", now);

    // new Date() with timestamp returns a number (timestamp)
    const d1: Date = new Date(now);
    console.log("ISO:", d1.toISOString());
    console.log("Date:", d1.getDate());
    console.log("Time:", d1.getTime());

    // Date.parse() returns a number (timestamp)
    const d2: Date = new Date(Date.parse("2026-07-14T08:00:00.000Z"));
    console.log("Parsed ISO:", d2.toISOString());
    console.log("Parsed Time:", d2.getTime());

    // Date getter functions
    console.log("Year:", d2.getFullYear());
    console.log("Month:", d2.getMonth());
    console.log("Day:", d2.getDate());
    console.log("Hours:", d2.getHours());
    console.log("Minutes:", d2.getMinutes());
    console.log("Seconds:", d2.getSeconds());

    // Date arithmetic
    const tomorrow: number = d2.getTime() + 1000 * 60 * 60 * 24;
    console.log("Tomorrow:", new Date(tomorrow).toISOString());
}
main();
