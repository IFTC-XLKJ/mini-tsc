import * as sqlite from "sqlite";

function main(): void {
  const db = sqlite.open(":memory:");

  db.createTable("users", {
    id: "integer primary key",
    name: "text",
    age: "int",
    email: "text",
  });

  db.insert("users", { name: "Alice", age: 30, email: "a@x.com" });
  db.insert("users", { name: "Bob", age: 25, email: "b@x.com" });
  db.insert("users", { name: "Carol", age: 35 });
  db.insert("users", { name: "Dave", age: 18, email: "d@x.com" });
  db.insert("users", { name: "Eve", age: 28, email: "e@x.com" });

  // plain count (no pagination)
  console.log("count all:", db.count("users"));
  console.log("count age>=25:", db.count("users", { age: { $gte: 25 } }));

  // findAndCount page 1
  const p1: any = db.findAndCount("users", null, {
    orderBy: "age",
    page: 1,
    pageSize: 2,
  });
  console.log("p1 total:", p1.total);
  console.log("p1 page:", p1.page);
  console.log("p1 pageSize:", p1.pageSize);
  console.log("p1 totalPages:", p1.totalPages);
  console.log("p1 rows len:", p1.rows.length);
  console.log("p1 first:", p1.rows[0].name);

  // findAndCount page 2
  const p2: any = db.findAndCount("users", null, {
    orderBy: "age",
    page: 2,
    pageSize: 2,
  });
  console.log("p2 total:", p2.total);
  console.log("p2 page:", p2.page);
  console.log("p2 first:", p2.rows[0].name);

  // findAndCount with where — total ignores limit
  const filtered: any = db.findAndCount(
    "users",
    { age: { $gte: 25 } },
    { orderBy: "-age", limit: 2, offset: 0 },
  );
  console.log("filtered total:", filtered.total);
  console.log("filtered rows len:", filtered.rows.length);
  console.log("filtered first:", filtered.rows[0].name);
  console.log("filtered totalPages:", filtered.totalPages);

  // limit/offset form
  const off: any = db.findAndCount("users", null, {
    orderBy: "name",
    limit: 3,
    offset: 1,
  });
  console.log("offset total:", off.total);
  console.log("offset rows:", off.rows.length);

  db.close();
  console.log("sqlite findAndCount tests passed!");
}
main();
