declare module "commander" {
    const program: Program;
    interface Program {
        program: Program;
        version(version: string): Program;
        name(name: string): Program;
        description(description: string): Program;
        parse(args: string[]): Program;
        opts(): any;
        option(option: string, description: string): Program;
        command(command: string): Command;
    }
    interface Command {
        description(description: string): Command;
        action(action: (...args: any[]) => void): Command;
    }
}