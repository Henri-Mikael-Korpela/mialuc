// get_name uses the constant NAME, which is defined outside the procedure.
// NAME is a procedure without parameters too, so it can be called as well.
console.log("get_name:", Str(get_name()));
console.log("NAME:", Str(NAME()));
// counter is a global variable, which is in the module scope too. It is declared with the attribute
// `static` and keeps its value between the calls, so each call counts on from the last one.
console.log("get_current_counter:", get_current_counter());
console.log("get_current_counter:", get_current_counter());
