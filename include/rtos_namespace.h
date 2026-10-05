#ifndef RTOS_NAMESPACE_H
#define RTOS_NAMESPACE_H

/* Define identically in all translation units of an instance. Empty by default;
 * use an identifier token such as app_, not a quoted string. API/configuration
 * macro names remain fixed; all C types, enum constants, functions and storage
 * symbols use RTOS_SYMBOL. Separate prefixes form independent instances. */
#ifndef RTOS_PREFIX
#define RTOS_PREFIX
#endif
#define RTOS_JOIN_RAW_IMPL(prefix, name) prefix##name
#define RTOS_JOIN_IMPL(prefix, name) RTOS_JOIN_RAW_IMPL(prefix, name)
#define RTOS_SYMBOL(name) RTOS_JOIN_IMPL(RTOS_PREFIX, name)

#endif /* RTOS_NAMESPACE_H */
