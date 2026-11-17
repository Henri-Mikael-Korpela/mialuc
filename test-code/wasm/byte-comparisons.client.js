// Arrays and array slices of bytes are compared by their bytes, with == and not=.
console.log("Two arrays with the same bytes are equal, and not= finds a different byte:",
    Boolean(same_arrays()), Boolean(different_arrays()));
console.log("A different number of bytes is not the same:", Boolean(different_lengths()));
console.log("An array and a slice, and two slices, can be compared:", Boolean(array_and_slice()),
    Boolean(two_slices()));
console.log("Bytes above 127 are numbers from 0 to 255:", Boolean(high_bytes()));
console.log("Nothing is the same as a slice with no bytes:", Boolean(nothing()));
console.log("Slices that are parameters:", Boolean(through_parameters()));
