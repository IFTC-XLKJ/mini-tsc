import * as fs from "fs";
import * as path from "path";

export interface InitOptions {
  projectRoot: string;
}

export async function init(projectName: string, options: InitOptions): Promise<void> {
  // Validate project name: only letters, underscores, and numbers
  if (!/^[a-zA-Z_][a-zA-Z0-9_]*$/.test(projectName)) {
    console.error("Error: project name can only contain letters, underscores, and numbers");
    process.exit(1);
  }

  const projectDir = path.resolve(options.projectRoot, projectName);

  // Check if directory already exists
  if (fs.existsSync(projectDir)) {
    console.error(`Error: directory '${projectName}' already exists`);
    process.exit(1);
  }

  // Create project directory
  fs.mkdirSync(projectDir, { recursive: true });

  // Create app.json
  const appJson = {
    name: projectName,
    version: "1.0.0",
    description: "",
    main: "main.ts",
    scripts: {
      build: "mini-tsc compile main.ts",
      transpile: "mini-tsc transpile main.ts",
    },
  };
  fs.writeFileSync(path.join(projectDir, "app.json"), JSON.stringify(appJson, null, 2) + "\n");

  // Create main.ts
  const mainTs = `// ${projectName} - mini-tsc project

function main(): void {
  console.log("Hello from ${projectName}!");
}

main();
`;
  fs.writeFileSync(path.join(projectDir, "main.ts"), mainTs);

  console.log(`Created project '${projectName}'`);
  console.log(`  ${projectDir}`);
  console.log(`  ├── app.json`);
  console.log(`  └── main.ts`);
}
