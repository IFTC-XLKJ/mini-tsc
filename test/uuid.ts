import * as uuid from "uuid";

function main(): void {
  const id1 = uuid.v4();
  console.log("UUID v4:", id1);
  console.log("Valid v4:", uuid.validate(id1));

  const id2 = uuid.v4();
  console.log("UUID v4 (2):", id2);

  const id3 = uuid.v7();
  console.log("UUID v7:", id3);
  console.log("Valid v7:", uuid.validate(id3));

  console.log("Invalid:", uuid.validate("not-a-uuid"));
  console.log("Valid lowercase:", uuid.validate("550e8400-e29b-41d4-a716-446655440000"));
  console.log("Valid uppercase:", uuid.validate("550E8400-E29B-41D4-A716-446655440000"));
}
