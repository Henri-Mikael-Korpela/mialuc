// A U32 arrives as a number, which is negative above 2147483647, and is returned as one, where
// WebAssembly takes any number and keeps its 32 bits.
const imports = {
    functions: {
        halve: (n) => (n >>> 0) / 2,
    },
};
