function main(): void {
    throw new Error("Test error");
}
try {
    main();
} catch (error: any) {
    console.log(error.message);
}
