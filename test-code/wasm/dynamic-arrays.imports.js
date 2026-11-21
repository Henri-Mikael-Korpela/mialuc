// The Mialu program says `import foreign js "functions"`, so the foreign procedures are the
// properties of `functions`. `printf` takes a `Str`, which arrives as two numbers, the address of its
// bytes in the memory of the program and their count, and an `Int`, which arrives as a BigInt.
// `Str(address, length)` reads the text, and the `%` in it stands for the number.
const imports = {
    functions: {
        printf: (address, length, number) => {
            console.log(Str(address, length).replace("%", number.toString()));
        },
    },
};
