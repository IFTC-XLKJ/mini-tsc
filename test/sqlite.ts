import * as sqlite from "sqlite";

function main(): void {
  // --- Simple CRUD (no SQL) ---
  const db = sqlite.open(":memory:");

  db.createTable("users", {
    id: "integer primary key",
    name: "text",
    age: "int",
  });

  const r1 = db.insert("users", { name: "Alice", age: 30 });
  console.log("insert changes:", r1.changes);
  console.log("insert rowid:", r1.lastInsertRowid);

  db.insert("users", { name: "Bob", age: 25 });
  db.insert("users", { name: "Carol", age: 30 });

  const alice = db.find("users", { name: "Alice" });
  console.log("find Alice age:", alice.age);

  const age30: any = db.findAll("users", { age: 30 });
  console.log("findAll age=30 count:", age30.length);

  const all: any = db.findAll("users");
  console.log("findAll all count:", all.length);

  const up = db.update("users", { age: 31 }, { name: "Alice" });
  console.log("update changes:", up.changes);

  const alice2 = db.find("users", { name: "Alice" });
  console.log("Alice new age:", alice2.age);

  const cnt: any = db.count("users");
  console.log("count all:", cnt);
  const cnt30: any = db.count("users", { age: 30 });
  console.log("count age=30:", cnt30);

  const del = db.remove("users", { name: "Bob" });
  console.log("remove changes:", del.changes);
  console.log("count after remove:", db.count("users"));

  // --- Low-level SQL still works ---
  db.exec("CREATE TABLE IF NOT EXISTS notes (id INTEGER PRIMARY KEY, body TEXT)");
  const ins = db.prepare("INSERT INTO notes (body) VALUES (?)");
  ins.run("hello");
  ins.finalize();
  const getStmt = db.prepare("SELECT body FROM notes WHERE id = ?");
  const note = getStmt.get(1);
  console.log("note body:", note.body);
  getStmt.finalize();

  db.dropTable("notes");
  db.close();
  console.log("sqlite crud tests passed!");
}
main();
