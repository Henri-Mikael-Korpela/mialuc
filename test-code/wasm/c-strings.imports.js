// A C_Str arrives as one number, the address of its first byte in the memory of the program. There
// is no length: the string ends at the first zero byte, so this goes through the bytes to find it.
const imports = {
    functions: {
        log_c_str: (address) => {
            const bytes = new Uint8Array(instance.exports.memory.buffer);
            let end = address;
            while (bytes[end] !== 0) {
                end += 1;
            }
            console.log("C string:", new TextDecoder().decode(bytes.subarray(address, end)));
        },
    },
};
