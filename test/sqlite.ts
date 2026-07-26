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

  // equality (unchanged)
  const alice = db.find("users", { name: "Alice" });
  console.log("eq Alice age:", alice.age);

  // $gt / $lte
  const adults: any = db.findAll("users", { age: { $gt: 18, $lte: 30 } });
  console.log("age 19..30 count:", adults.length);

  // $ne
  const notBob: any = db.findAll("users", { name: { $ne: "Bob" } });
  console.log("not Bob count:", notBob.length);

  // $like
  const aNames: any = db.findAll("users", { name: { $like: "A%" } });
  console.log("name like A% count:", aNames.length);

  // $in
  const inNames: any = db.findAll("users", { name: { $in: ["Alice", "Carol"] } });
  console.log("name in Alice|Carol count:", inNames.length);

  // $nin
  const ninNames: any = db.findAll("users", { name: { $nin: ["Bob", "Dave"] } });
  console.log("name nin Bob|Dave count:", ninNames.length);

  // $null true / false
  const noEmail: any = db.findAll("users", { email: { $null: true } });
  console.log("email IS NULL count:", noEmail.length);
  const hasEmail: any = db.findAll("users", { email: { $null: false } });
  console.log("email IS NOT NULL count:", hasEmail.length);

  // combined columns AND operators
  const filtered: any = db.findAll("users", {
    age: { $gte: 25 },
    name: { $ne: "Bob" },
  });
  console.log("age>=25 and not Bob count:", filtered.length);

  // update / remove with operators
  const up = db.update("users", { age: 31 }, { age: { $lt: 20 } });
  console.log("update age<20 changes:", up.changes);
  const dave = db.find("users", { name: "Dave" });
  console.log("Dave new age:", dave.age);

  const del = db.remove("users", { age: { $gt: 30 } });
  console.log("remove age>30 changes:", del.changes);
  console.log("count left:", db.count("users"));

  db.close();
  console.log("sqlite where-ops tests passed!");
}
main();
