/** Minimal Node.js `os` ambient types for mini-tsc. */
declare module "os" {
    interface CpuInfo {
        model?: string;
        speed?: number;
        times?: { user: number; nice: number; sys: number; idle: number; irq: number };
    }

    interface UserInfo {
        username: string;
        uid?: number;
        gid?: number;
        shell?: string;
        homedir?: string;
    }

    function platform(): string;
    function hostname(): string;
    function totalmem(): number;
    function freemem(): number;
    function arch(): string;
    function cpus(): CpuInfo[];
    function userInfo(): UserInfo;
    function type(): string;
    function release(): string;
    function uptime(): number;
    function loadavg(): number[];
    function homedir(): string;
    function tmpdir(): string;
    function version(): string;
    function machine(): string;

    const EOL: string;
    const devNull: string;

    export {
        platform,
        hostname,
        totalmem,
        freemem,
        arch,
        cpus,
        userInfo,
        type,
        release,
        uptime,
        loadavg,
        homedir,
        tmpdir,
        version,
        machine,
        EOL,
        devNull,
        CpuInfo,
        UserInfo,
    };
}
