/** C language data-type descriptors for mini-tsc (FFI companion). */
declare module "ctype" {
    class CType {
        name: string;
        size: number;
        alignment: number;
        signed: boolean;
        constructor(name: string, size: number, alignment: number, signed: boolean);
    }

    type char = CType | string;
    type schar = CType | string;
    type uchar = CType | string;
    type short = CType | number;
    type ushort = CType | number;
    type int = CType | number;
    type uint = CType | number;
    type long = CType | number;
    type ulong = CType | number;
    type longlong = CType | number;
    type ulonglong = CType | number;
    type float = CType | number;
    type double = CType | number;
    type bool = CType | boolean;
    type pointer = CType | number;
    type size_t = CType | number;
    type ptrdiff_t = CType | number;

    type CTypes = char | schar | uchar | short | ushort | int | uint | long | ulong | longlong | ulonglong | float | double | bool | pointer | size_t | ptrdiff_t;

    function sizeof(type: CTypes): number;
    function alignof(type: CTypes): number;
    /** Get memory address of a variable or constant (returns hex string like "0x7ffd5c8a"). */
    function getptr(type: CTypes): string;
    /** Compare two values for equality. */
    function equal(a: CTypes, b: CTypes): boolean;

    export {
        CType,
        char,
        schar,
        uchar,
        short,
        ushort,
        int,
        uint,
        long,
        ulong,
        longlong,
        ulonglong,
        float,
        double,
        bool,
        pointer,
        size_t,
        ptrdiff_t,
        sizeof,
        alignof,
        getptr,
        equal,
    };
}
