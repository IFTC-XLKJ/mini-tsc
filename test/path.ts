import * as path from "path";

function main(): void {
    console.log(__dirname);
    console.log(__filename);
    console.log(path.join("foo", "bar"));
    console.log(path.basename("foo/bar/baz.txt"));
    console.log(path.dirname("foo/bar/baz.txt"));
    console.log(path.extname("file.txt"));
    console.log(path.resolve("foo/bar/baz.txt"));
    console.log(path.parse("foo/bar/baz.txt"));
    console.log(path.format(path.parse("foo/bar/baz.txt")));
    console.log(path.isAbsolute("/foo/bar/baz.txt"));
    console.log(path.normalize("foo/bar/baz.txt"));
    console.log(path.relative("foo/bar/baz.txt", "foo/bar/baz.txt"));
}
main();
