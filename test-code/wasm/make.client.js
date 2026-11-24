// Each procedure returns a number, which is printed with the number that the language gives.
console.log("bytes_are_zero_and_assigned, 1616207:", bytes_are_zero_and_assigned());
console.log("other_elements, 63:", other_elements());
console.log("empty, 0:", empty());
console.log("passed_to_procedures, 290:", passed_to_procedures());
console.log("outlive_the_procedure, 7290:", outlive_the_procedure());
console.log("made_again_and_again, 49995000:", made_again_and_again());
console.log("grows_the_memory, 300005:", grows_the_memory());
try {
    negative_count(-1);
    console.log("negative_count did not trap");
} catch (error) {
    console.log("A negative count traps:", error.message);
}
