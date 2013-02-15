#include <linux/errno.h>
#include <linux/mm_types.h>
#include <linux/types.h>
#include <linux/hash.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/slab.h>
#include <linux/oom.h>
#include <linux/freezer.h>
#include <linux/memory.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/swap.h>
#include <linux/sksm.h>
#include <linux/pid.h>

MODULE_LICENSE("GPL");

//// for sys filesystem.
#ifdef CONFIG_SYSFS
#define SKSM_ATTR_RO(_name) static struct kobj_attribute _name##_attr = __ATTR_RO(_name)
#define SKSM_ATTR(_name) static struct kobj_attribute _name##_attr = \
		__ATTR(_name, 0644, _name##_show, _name##_store)

static ssize_t sth_sksm_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "Hello, XXXXXXXXXXX. %u\n", vma_count);
}
SKSM_ATTR_RO(sth_sksm);

static ssize_t mytest_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
}
static ssize_t mytest_store(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf, size_t count)
{
}
SKSM_ATTR(mytest);

static struct attribute *sksm_attrs[] = { 
	&sth_sksm_attr, 
	&mytest_attr,
	NULL 
};
static struct attribute_group sksm_attr_group = {
	.attrs = sksm_attrs,
	.name = "fable", 
};

static int __init sksm_init(void)
{
}

static void __exit sksm_exit(void)
{
} 

module_init(sksm_init);
module_exit(sksm_exit);
#endif

