#include "device_file.h"
#include <linux/init.h>       /* module_init, module_exit */
#include <linux/module.h>     /* version info, MODULE_LICENSE, MODULE_AUTHOR, printk() */

MODULE_DESCRIPTION("Ilies Linux block driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexandru Ilies");

/*===============================================================================================*/
static int __init simple_block_driver_init(void)
{
    int result = 0;
    printk( KERN_NOTICE "IliesBlock-Driver: Initialization started\n" );

    result = simple_block_register();
    return result;
}

/*===============================================================================================*/
static void __exit simple_block_driver_exit(void)
{
    printk( KERN_NOTICE "IliesBlock-Driver: Exiting\n" );
    simple_block_unregister();
}

/*===============================================================================================*/
module_init(simple_block_driver_init);
module_exit(simple_block_driver_exit);
