// A for statement over a string goes through its characters, as Runes, which are plain numbers for
// JavaScript: their code points.
console.log("Héllo has 5 characters, though 6 bytes:", count_characters());
console.log("The code points of aé€🚀z add up to 137456:", sum_of_code_points());
console.log("The vowels of Héllo, wörld, which are é, o and ö:", count_vowels());
console.log("break and continue, 2 and 3:", count_before_comma(), count_without_commas());
console.log("A string that has no value has no characters:", count_nothing());
