function main(): void {
    throw new Error("Test error");
}
try {
    main();
} catch (error) {
    console.log(error.message);
}
