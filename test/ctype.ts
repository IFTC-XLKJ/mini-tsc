import { sizeof, alignof, getptr, equal, CType, char, int, bool, float as cfloat, double as cdouble, short, long, pointer } from "ctype";

function main() {
    // Test char type (string)
    const char_test: char = "a";
    console.log(`char value: ${char_test}`);
    console.log(`Size of char: ${sizeof(char_test)}, Alignment: ${alignof(char_test)}`);
    console.log(`Address of char_test: ${getptr(char_test)}`);

    // Test int type (number)
    const int_type: int = 42;
    console.log(`int value: ${int_type}`);
    console.log(`Size of int: ${sizeof(int_type)}, Alignment: ${alignof(int_type)}`);
    console.log(`Address of int_type: ${getptr(int_type)}`);

    // Test bool type (boolean)
    const bool_type: bool = true;
    console.log(`bool value: ${bool_type}`);
    console.log(`Size of bool: ${sizeof(bool_type)}, Alignment: ${alignof(bool_type)}`);
    console.log(`Address of bool_type: ${getptr(bool_type)}`);

    // Test float type (number)
    const float_type: cfloat = 3.14;
    console.log(`float value: ${float_type}`);
    console.log(`Size of float: ${sizeof(float_type)}, Alignment: ${alignof(float_type)}`);
    console.log(`Address of float_type: ${getptr(float_type)}`);

    // Test double type (number)
    const double_type: cdouble = 2.718281828;
    console.log(`double value: ${double_type}`);
    console.log(`Size of double: ${sizeof(double_type)}, Alignment: ${alignof(double_type)}`);
    console.log(`Address of double_type: ${getptr(double_type)}`);

    // Test short type (number)
    const short_type: short = 1000;
    console.log(`short value: ${short_type}`);
    console.log(`Size of short: ${sizeof(short_type)}, Alignment: ${alignof(short_type)}`);
    console.log(`Address of short_type: ${getptr(short_type)}`);

    // Test long type (number)
    const long_type: long = 100000;
    console.log(`long value: ${long_type}`);
    console.log(`Size of long: ${sizeof(long_type)}, Alignment: ${alignof(long_type)}`);
    console.log(`Address of long_type: ${getptr(long_type)}`);

    // Test pointer type (number - treated as address)
    const pointer_type: pointer = 0;
    console.log(`pointer value: ${pointer_type}`);
    console.log(`Size of pointer: ${sizeof(pointer_type)}, Alignment: ${alignof(pointer_type)}`);
    console.log(`Address of pointer_type: ${getptr(pointer_type)}`);

    // Test equal function with variables
    console.log("\n--- Testing equal() ---");
    const a: int = 42;
    const b: int = 42;
    const c: int = 43;
    console.log(`equal(42, 42): ${equal(a, b)}`);
    console.log(`equal(42, 43): ${equal(a, c)}`);

    const s1: char = "hello";
    const s2: char = "hello";
    const s3: char = "world";
    console.log(`equal("hello", "hello"): ${equal(s1, s2)}`);
    console.log(`equal("hello", "world"): ${equal(s1, s3)}`);

    const t: bool = true;
    const f: bool = false;
    console.log(`equal(true, true): ${equal(t, t)}`);
    console.log(`equal(true, false): ${equal(t, f)}`);

    console.log("\n=== All type tests passed! ===");
}
main();
