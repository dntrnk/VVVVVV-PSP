#ifndef UNREACHABLE_H
#define UNREACHABLE_H

__attribute__((noreturn)) static inline void VVV_unreachable(void)
{
    __builtin_unreachable();
}

#endif /* UNREACHABLE_H */
