# Errors as interface

The error messages of a compiler are the part of it that a programmer reads the most, so they are an interface, and they can be designed. This document collects the mistakes that people make most often when they write programs in other languages: C, C++, Java, Odin, C#, Python, Kotlin, TypeScript and JavaScript. It is the material for deciding what the Mialu compiler should say when a programmer makes the same mistake, and how it should help to fix it.

Every entry has the mistake, a short example of it and what happens in that language. Below each is a **Mialu** line that is left to fill in. See [mialuc](mialu-cli.md#errors) for how the compiler lays out an error, [Error codes](error-codes.md) for the errors that it has, and the [language specification](mialu-programming-language.md) for what the language does.

## How to fill in an entry

An entry is filled in when the Mialu line says what the compiler does, and what it should do when that is not enough:

```
**Mialu:**
- Status: caught at compile time | caught at run time (trap) | not possible in the language | not caught
- Message: the exact text of the error, with its code, line and column, as the compiler prints it
- Fix: the suggestion that the message makes, or the fixed code
- Test: the file in test-code/errors that has the mistake, so that the message is tested, see `scripts/verify_errors.py`
```

A good message says what is wrong, where, why the rule exists, and what to write instead, and it names the thing in the program, not only its kind. The **Message** of an entry should follow that, and the **Fix** should be code that the programmer can copy. Not every mistake is to be caught: a mistake that the language makes impossible, such as pointer arithmetic, is a good answer, and the entry says which rule of the specification does it.

Until an entry is filled in it says `TODO`.

## Mistakes by kind

The same few kinds of mistakes appear in every language below. A kind that has many entries is where the compiler can do the most.

| Kind | Where it appears below |
|---|---|
| Memory that is invalid: out of bounds, null, freed, uninitialized | C, C++, Odin, Java, C#, Kotlin |
| Types that are mixed: implicit conversions, wrong comparisons | C, C++, JavaScript, Python, TypeScript |
| Syntax slips: `=` for `==`, missing braces, missing or extra semicolon, indentation | C, C++, Java, JavaScript, Python |
| Names: unused, shadowed, misspelled, not in scope | Python, JavaScript, TypeScript, Kotlin |
| Resources that are not released | C, C++, Java, C#, Odin, Python |
| Errors that are ignored | C, Odin, JavaScript, Java, Python |
| Control flow: fall-through, forgotten `break`, unreachable code, missing `return` | C, C++, Java, JavaScript, Kotlin |
| Integers: overflow, signed and unsigned mixed, division | C, C++, Java, C#, Odin |
| Strings and characters: bytes and characters mixed, encoding | C, C++, Python, JavaScript, Java |
| Concurrency: races, deadlocks | C, C++, Java, C#, Kotlin |

## C

### C1. Out-of-bounds access of an array

```c
int a[4];
a[4] = 1;
```

Undefined behaviour. It writes past the array, and often goes unnoticed until a later crash.

**Mialu:** TODO

### C2. Dereferencing a null or dangling pointer

```c
int *p = NULL;
*p = 3;
char *f(void) { char buf[8]; return buf; }
```

Undefined behaviour. A compiler may remove the checks around it.

**Mialu:** TODO

### C3. `=` where `==` was meant

```c
if (x = 5) { ... }
```

Compiles, assigns, and is always true.

**Mialu:** TODO

### C4. Uninitialized variable

```c
int total;
total += 1;
```

The value is whatever was in memory.

**Mialu:** TODO

### C5. Memory leak, double free and use after free

```c
char *s = malloc(10);
free(s);
s[0] = 'x';
```

**Mialu:** TODO

### C6. Buffer overflow with unchecked string functions

```c
char name[8];
strcpy(name, "a long name");
```

**Mialu:** TODO

### C7. Missing `break` in a `switch`

```c
switch (c) {
    case 1: a();
    case 2: b();
}
```

Case 1 also runs `b()`.

**Mialu:** TODO

### C8. Integer overflow, and signed with unsigned

```c
unsigned n = 0;
if (n - 1 > 0) { ... }
int x = INT_MAX + 1;
```

**Mialu:** TODO

### C9. Ignored return value of a function that can fail

```c
FILE *f = fopen(path, "r");
fread(buf, 1, n, f);
```

`f` can be `NULL`, and the result of `fread` says how much was read.

**Mialu:** TODO

### C10. Wrong format string

```c
printf("%d", 3.5);
printf("%s", 7);
```

**Mialu:** TODO

### C11. Operator precedence

```c
if (flags & MASK == 0) { ... }
```

`==` binds tighter than `&`.

**Mialu:** TODO

### C12. Array parameter that is really a pointer

```c
void f(int a[10]) { sizeof(a); }
```

`sizeof(a)` is the size of a pointer, not of the array.

**Mialu:** TODO

## C++

### C++1. Dangling reference or iterator

```cpp
const std::string &s = make().name();
v.push_back(1);
auto it = v.begin(); v.push_back(2); *it;
```

**Mialu:** TODO

### C++2. Use after move

```cpp
auto b = std::move(a);
a.size();
```

**Mialu:** TODO

### C++3. Forgotten `virtual` destructor, and slicing

```cpp
struct Base { ~Base() {} };
Base *p = new Derived;
delete p;
void f(Base b);
f(derived);
```

**Mialu:** TODO

### C++4. Manual `new` and `delete` that do not match

```cpp
int *a = new int[10];
delete a;
```

Should be `delete[] a`.

**Mialu:** TODO

### C++5. Implicit conversions and narrowing

```cpp
int n = 3.9;
std::vector<int> v(-1);
void f(std::string s);
f(nullptr);
```

**Mialu:** TODO

### C++6. Undefined order of evaluation

```cpp
i = i++ + 1;
f(a(), b());
```

**Mialu:** TODO

### C++7. Template errors

```cpp
std::sort(list.begin(), list.end());
```

The message is hundreds of lines, because `std::list` has no random access iterators.

**Mialu:** TODO

### C++8. Data races

```cpp
int counter;
std::thread t([&] { counter++; });
counter++;
```

**Mialu:** TODO

### C++9. Missed `const`, wrong copy, and missing `return`

```cpp
int f(int x) { if (x) return 1; }
void g(std::vector<int> v);
```

**Mialu:** TODO

## Java

### Java1. `NullPointerException`

```java
String s = map.get("key");
s.length();
```

**Mialu:** TODO

### Java2. Comparing strings and objects with `==`

```java
if (name == "Bob") { ... }
```

Compares references, not text.

**Mialu:** TODO

### Java3. Integer overflow, and integer division

```java
int big = Integer.MAX_VALUE + 1;
double d = 1 / 2;
```

**Mialu:** TODO

### Java4. Missing `break` in a `switch`, and a stray semicolon

```java
if (done);
    finish();
for (int i = 0; i < n; i++);
```

**Mialu:** TODO

### Java5. Unclosed resources

```java
InputStream in = new FileInputStream(path);
```

Must be closed, for example with try-with-resources.

**Mialu:** TODO

### Java6. Swallowed exceptions and the wrong one caught

```java
try { work(); } catch (Exception e) { }
```

**Mialu:** TODO

### Java7. Modifying a collection while iterating it

```java
for (String s : list) list.remove(s);
```

`ConcurrentModificationException`.

**Mialu:** TODO

### Java8. Missing `equals` and `hashCode`, and mutable keys

```java
class Point { int x, y; }
set.contains(new Point(1, 2));
```

**Mialu:** TODO

### Java9. Race conditions and a missing `synchronized`

```java
count++;
```

Not atomic when threads share it.

**Mialu:** TODO

## Odin

### Odin1. Forgotten `delete` and leaked memory

```odin
s := make([]int, 10)
```

No `defer delete(s)`.

**Mialu:** TODO

### Odin2. Returning a pointer to a local, or a slice of a temporary

```odin
f :: proc() -> ^int { x := 1; return &x }
```

**Mialu:** TODO

### Odin3. Ignored error and `ok` values

```odin
data, _ := os.read_entire_file(path)
```

**Mialu:** TODO

### Odin4. Out-of-bounds index with bounds checking turned off

```odin
a: [4]int
i := 4
a[i] = 1
```

Caught by default, but not with `-no-bounds-check`.

**Mialu:** TODO

### Odin5. Mixing of integer types with no implicit conversion

```odin
a: i32 = 1
b: i64 = a
```

**Mialu:** TODO

### Odin6. Allocator confusion: freeing with another allocator than the one that allocated

```odin
x := new(int, context.temp_allocator)
free(x)
```

**Mialu:** TODO

### Odin7. Shadowing and the unused variable error

```odin
x := 1
x := 2
```

**Mialu:** TODO

### Odin8. Union and `switch` that is not exhaustive

```odin
switch v in value { case int: ... }
```

**Mialu:** TODO

## C#

### C#1. `NullReferenceException`

```csharp
string s = dict["a"];
s.Length;
```

**Mialu:** TODO

### C#2. Forgotten `Dispose` or `using`

```csharp
var f = new FileStream(path, FileMode.Open);
```

**Mialu:** TODO

### C#3. `async` without `await`, and `.Result` deadlocks

```csharp
var x = GetAsync();
x.Result;
```

**Mialu:** TODO

### C#4. Modified closure variable in a loop, and a collection that changes while it is iterated

```csharp
foreach (var x in list) list.Remove(x);
```

**Mialu:** TODO

### C#5. Integer overflow in `unchecked` code, and integer division

```csharp
int a = int.MaxValue + 1;
double d = 1 / 2;
```

**Mialu:** TODO

### C#6. Comparing strings with `==` in culture-dependent ways, and floats with `==`

```csharp
if (0.1 + 0.2 == 0.3) { ... }
```

**Mialu:** TODO

### C#7. Structs that are copied, so that a change is lost

```csharp
list[0].X = 1;
```

**Mialu:** TODO

### C#8. Missing `break` in a `switch` section

```csharp
case 1: a();
case 2: b();
```

A compile error in C#, but a common slip.

**Mialu:** TODO

## Python

### Python1. Indentation errors and mixed tabs and spaces

```python
if x:
print(x)
```

**Mialu:** TODO

### Python2. Mutable default argument

```python
def add(item, items=[]):
    items.append(item)
    return items
```

**Mialu:** TODO

### Python3. Misspelled name found only when the line runs

```python
total = 0
totl += 1
```

`NameError` at run time.

**Mialu:** TODO

### Python4. `UnboundLocalError`: assigning to a global in a function

```python
count = 0
def inc():
    count += 1
```

**Mialu:** TODO

### Python5. Modifying a list while iterating it

```python
for x in items:
    items.remove(x)
```

**Mialu:** TODO

### Python6. Late binding in closures

```python
fs = [lambda: i for i in range(3)]
```

All return 2.

**Mialu:** TODO

### Python7. `is` for equality, and `==` for `None`

```python
if x is 5: ...
```

**Mialu:** TODO

### Python8. Type mix-ups that only fail when run

```python
"age: " + 5
```

**Mialu:** TODO

### Python9. Catching everything, and silent `except`

```python
try:
    work()
except:
    pass
```

**Mialu:** TODO

### Python10. Files not closed, and bytes and `str` mixed

```python
f = open(path)
open(path, "rb").read() + "text"
```

**Mialu:** TODO

## Kotlin

### Kotlin1. `!!` on a null value

```kotlin
val n: String? = map["a"]
n!!.length
```

**Mialu:** TODO

### Kotlin2. Platform types from Java that are not null-checked

```kotlin
val s = javaObject.name
s.length
```

**Mialu:** TODO

### Kotlin3. Unintended `val` and `var`, and a `lateinit` that is not initialized

```kotlin
lateinit var name: String
name.length
```

**Mialu:** TODO

### Kotlin4. `when` that is not exhaustive as a statement

```kotlin
when (color) { Color.RED -> a() }
```

**Mialu:** TODO

### Kotlin5. Coroutines: blocking in a coroutine, and a cancelled scope

```kotlin
runBlocking { delay(1) }
```

**Mialu:** TODO

### Kotlin6. Shadowed names and an unused result

```kotlin
list.map { it + 1 }
```

The result is dropped.

**Mialu:** TODO

### Kotlin7. Smart casts that fail for `var` properties

```kotlin
if (x != null) x.length
```

**Mialu:** TODO

## TypeScript

### TypeScript1. `any` that turns off checking

```ts
function f(x: any) { return x.foo.bar; }
```

**Mialu:** TODO

### TypeScript2. `undefined` and `null` not handled

```ts
const u = users.find(x => x.id === 1);
u.name;
```

**Mialu:** TODO

### TypeScript3. Types that disappear at run time

```ts
if (x instanceof Point[]) { ... }
```

**Mialu:** TODO

### TypeScript4. Wrong assertion with `as`

```ts
const n = JSON.parse(text) as Config;
```

Nothing checks that the data has the shape of `Config`.

**Mialu:** TODO

### TypeScript5. Excess and missing properties, and optional properties

```ts
type P = { name: string; age?: number };
p.age + 1;
```

**Mialu:** TODO

### TypeScript6. Floating promises and a missing `await`

```ts
save();
```

**Mialu:** TODO

### TypeScript7. Union that is not narrowed, and a `switch` that is not exhaustive

```ts
type S = "a" | "b";
function f(s: S) { switch (s) { case "a": return 1; } }
```

**Mialu:** TODO

## JavaScript

### JavaScript1. `undefined is not a function` and `Cannot read properties of undefined`

```js
const a = obj.items.length;
```

**Mialu:** TODO

### JavaScript2. Type coercion with `==` and `+`

```js
"5" + 3;
0 == "";
[] + {};
```

**Mialu:** TODO

### JavaScript3. Wrong `this`

```js
setTimeout(obj.method, 10);
```

**Mialu:** TODO

### JavaScript4. `var` hoisting and closures in loops

```js
for (var i = 0; i < 3; i++) setTimeout(() => console.log(i));
```

Prints 3, three times.

**Mialu:** TODO

### JavaScript5. Floating-point numbers and large integers

```js
0.1 + 0.2 === 0.3;
9007199254740993;
```

**Mialu:** TODO

### JavaScript6. Forgotten `await`, and unhandled rejections

```js
const data = fetch(url).json();
```

**Mialu:** TODO

### JavaScript7. Automatic semicolon insertion

```js
return
    { ok: true };
```

Returns `undefined`.

**Mialu:** TODO

### JavaScript8. `NaN` and sorting numbers as strings

```js
[10, 9, 1].sort();
NaN === NaN;
```

**Mialu:** TODO

### JavaScript9. A misspelled property is created, not rejected

```js
user.nmae = "Bob";
```

**Mialu:** TODO

## Next steps

1. Fill in the **Mialu** line of each entry, starting with the kinds that have the most entries in the table above.
2. For each entry that the compiler catches, add a program to `test-code` that has the mistake, with its message as the expected result, so that the wording cannot change by accident.
3. For each entry that the compiler does not catch yet, decide if the language should prevent it, the compiler should warn, or it is out of scope, and write down which and why.
4. Where the same message could be better, write the better one here first, then change the compiler.
