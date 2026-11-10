#ifndef MIALU_VERSION_H
#define MIALU_VERSION_H

/**
 * The version of the compiler, the only place where it is written down. It follows semantic
 * versioning. These are macros because they are used as C string literal pieces and as numbers
 * in the preprocessor; they have no side effects and use no arguments.
 */
#define MIALU_VERSION_MAJOR 0
#define MIALU_VERSION_MINOR 1
#define MIALU_VERSION_PATCH 0

#define MIALU_VERSION_STRING_HELPER(text) #text
#define MIALU_VERSION_STRING_FROM(major, minor, patch) \
    MIALU_VERSION_STRING_HELPER(major) "." MIALU_VERSION_STRING_HELPER(minor) "." \
    MIALU_VERSION_STRING_HELPER(patch)

// "0.1.0"
#define MIALU_VERSION_STRING \
    MIALU_VERSION_STRING_FROM(MIALU_VERSION_MAJOR, MIALU_VERSION_MINOR, MIALU_VERSION_PATCH)

#endif
