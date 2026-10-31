#ifndef MIALU_STATIC_ASSERT_H
#define MIALU_STATIC_ASSERT_H

/**
 * C99 has no `static_assert`, so a failed condition makes the array size negative, which is a
 * compile-time error. `name` must be unique in the translation unit and should say what is being
 * checked. This is a macro because it needs the condition as an expression, and it evaluates it
 * only once.
 */
#define STATIC_ASSERT(condition, name) typedef char name[1 - 2 * !(condition)]

#endif
