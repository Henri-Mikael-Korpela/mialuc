// A foreign procedure is defined in JavaScript. The `import foreign js` line names the
// object that has it, and `...` stands where the body would be.

import foreign js "functions"

foreign js ->
    alert :: proc(msg: Str) -> ...

/// Calls the procedure `alert` of the JavaScript object `functions`.
print_hello :: proc() ->
    alert("Hello, world!")