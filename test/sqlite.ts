import * as sqlite from "sqlite";

function main(): void {
  const db = sqlite.open("test.db");
  db.exec("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)");

  const insert = db.prepare("INSERT INTO users (name, age) VALUES (?, ?)");
  const r1 = insert.run("Alice", 30);
  console.log("changes:", r1.changes);
  console.log("lastInsertRowid:", r1.lastInsertRowid);

  insert.run("Bob", 25);
  insert.finalize();

  const getStmt = db.prepare("SELECT name, age FROM users WHERE name = ?");
  const row = getStmt.get("Alice");
  console.log("Alice age:", row.age);
  getStmt.finalize();

  const allStmt = db.prepare("SELECT name, age FROM users ORDER BY age DESC");
  const rows: any = allStmt.all();
  console.log("count:", rows.length);
  console.log("first name:", rows[0].name);
  allStmt.finalize();

  const ver = db.pragma("user_version");
  console.log("pragma user_version:", ver);

  db.close();
  console.log("sqlite tests passed!");
}
main();
