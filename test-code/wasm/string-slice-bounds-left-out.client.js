// A Str is returned as the address of its bytes and their count, which Str() reads.
console.log("s[0:12], s[:12], s[0:] and s[:] are the same string, 0:", whole_string());
console.log("\"hello, world\"[:5] and [7:] are hello and world:", Str(start()), Str(end()));
console.log("s[7:7 + 3] and s[7:][:3] are both wor:", Str(chunk(7, 3)), Str(chunk_of_a_slice(7, 3)));
console.log("t[2:][:1] of \"añ🚀é!\" is 🚀, t[2:] has 3 characters and t[:2] has 2:", Str(characters()),
    character_count());
try {
    past_the_end(6);
} catch (error) {
    console.log("\"hello\"[6:] traps:", error.message);
}
console.log("\"hello\"[5:] has no characters, and does not trap:", past_the_end(5));
