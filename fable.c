#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");

static int count;

static int fable_init(void)
{
	count++;
	printk(KERN_ALERT"HELLO, fable %d", count);
	return 0;
}

static void fable_exit(void)
{
	printk(KERN_ALERT"GOODBYE, fable");
}

module_init(fable_init);
module_exit(fable_exit);
