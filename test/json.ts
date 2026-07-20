function main(): void {
    // JSON.stringify
    const obj = { name: "Alice", age: 30, active: true };
    const json = JSON.stringify(obj, null, 4);
    console.log(json);

    // JSON.parse
    const parsed = JSON.parse('{"x": 10, "y": "hello"}');
    console.log(parsed);

    // JSON.parse with array
    const arr = JSON.parse('[1, 2, 3, "four"]');
    console.log(arr);

    // JSON.isRawJSON
    console.log(JSON.isRawJSON('{"a": 1}'));
    console.log(JSON.isRawJSON('hello'));

    // JSON.rawJSON
    const raw = JSON.rawJSON('{"b": 2}');
    console.log(raw);

    // Nested object stringify
    const nested = { a: { b: 1 }, c: [1, 2] };
    console.log(JSON.stringify(nested));
}
main();
