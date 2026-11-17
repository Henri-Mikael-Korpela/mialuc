// An I32 arrives as a plain number and is returned as one, where an Int is a BigInt.
const imports = {
    functions: {
        average: (a, b) => (a + b) / 2 | 0,
    },
};
