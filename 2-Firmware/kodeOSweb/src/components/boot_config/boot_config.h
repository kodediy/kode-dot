#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include <stdbool.h>

// Declaraciones de funciones
void set_temporary_boot_flag(bool value);
bool get_temporary_boot_flag(void);
void clear_temporary_boot_flag(void);

#endif // BOOT_CONFIG_H
