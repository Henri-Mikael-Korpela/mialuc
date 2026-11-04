// The Mialu program says `import foreign js "functions"`, so the foreign procedures are the
// properties of `functions`. Each is a function of the same name that takes the values of the
// parameters. An Int arrives as a BigInt, and so must be returned. A Str arrives as two numbers:
// the address of its bytes in the memory of the program and their count.
// Str(address, length) reads them, because `instance` has the memory once the program exists.
const imports = {
    functions: {
        alert: (address, length) => {
            // A web worker has no alert, so the message goes to the console.
            console.log("alert:", Str(address, length));
        },
    },
};
