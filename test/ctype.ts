import { sizeof, alignof, CType, char, int } from "ctype";

function main() {
    const char_test: char = "a";
    console.log(char_test);
    const int_type: int = 42;
    console.log(int_type);
    const char_size = sizeof(char_test);
    const char_align = alignof(char_test);
    console.log(`Size of char: ${char_size}, Alignment of char: ${char_align}`);
    const int_size = sizeof(int_type);
    const int_align = alignof(int_type);
    console.log(`Size of int: ${int_size}, Alignment of int: ${int_align}`);
}
main();
