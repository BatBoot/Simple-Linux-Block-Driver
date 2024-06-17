#ifndef DEVICE_FILE_H_
#define DEVICE_FILE_H_
#include <linux/compiler.h> /* __must_check */

__must_check int simple_block_register(void);
void simple_block_unregister(void);

#endif
