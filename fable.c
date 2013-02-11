#include <linux/errno.h>
#include <linux/mm_types.h>
#include <linux/types.h>
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

static pid_t the_pid;  // for test ...
static unsigned int vma_count;	// 记有多少个vma被注册
static struct kmem_cache *vam_sksm_info_cache;

#define SKSM_KMEM_CACHE(__struct, __flags) kmem_cache_create("SKSM_"#__struct, \
	sizeof(struct __struct), __alignof__(struct __struct), (__flags), NULL)
#define VMA_SKSM_INFO_LIST_COUNT 3	// 有多少个存放vma_sksm_info_list的队列.

struct rb_root vma_sksm_info_tree = RB_ROOT;	// 红黑树，保存了所有的vma_sksm_info. 
static spinlock_t vma_sksm_info_lock = SPIN_LOCK_UNLOCKED;
struct vma_sksm_info {
    struct vm_area_struct *vma;
    struct rb_node tree_node;	// all vma_sksm_info data are stored in a red black tree.

    struct list_head level_list;	// vma_sksm_info 被存在哪个级别的list中.
    // struct rb_node level_node;	// 这个级别的vma_sksm_info也用红黑树存起来. 
};
static struct list_head vma_sksm_info_heads[VMA_SKSM_INFO_LIST_COUNT];
//static struct rb_root vma_sksm_info_trees[VMA_SKSM_INFO_LIST_COUNT];
//static spinlock_t vma_sksm_info_locks[VMA_SKSM_INFO_LIST_COUNT];

static int init_vma_sksm_infos(void)
{
    int i;
    for (i = 0; i < VMA_SKSM_INFO_LIST_COUNT; i++)
    {
	INIT_LIST_HEAD(&vma_sksm_info_heads[i]);
    }

    return 0;
};

static inline struct vma_sksm_info *alloc_vma_sksm_info(void)
{
    struct vma_sksm_info *vsi;

    vsi = kmem_cache_zalloc(vam_sksm_info_cache, GFP_KERNEL);

    return vsi;
}

static inline void free_vma_sksm_info(struct vma_sksm_info *vsi)
{
    vsi->vma = NULL;
    kmem_cache_free(vam_sksm_info_cache, vsi);
}

int is_mergeable_area(struct vm_area_struct *vma)
{
        unsigned long flags = vma->vm_flags;
        
        if(vma->vm_file)
                return 0;
        
        if(flags&(VM_SHARED|VM_MAYSHARE|VM_PFNMAP|VM_IO|VM_DONTEXPAND|
                VM_HUGETLB|VM_NONLINEAR|VM_MIXEDMAP))
                return 0;
        
        return 1;
}

int __register_vma_sksm(struct vm_area_struct *vma)
{
        int ret;
	struct rb_node **new = &vma_sksm_info_tree.rb_node;
	struct rb_node *parent = NULL;
	struct vma_sksm_info *vsi;

	spin_lock(&vma_sksm_info_lock);
	
	while(*new)
	{
		vsi = rb_entry(*new, struct vma_sksm_info, tree_node);
		parent = *new;
		if(vma<vsi->vma)
                {
                       new = &parent->rb_left; 
                }
                else if(vma>vsi->vma)
                {
                       new = &parent->rb_right; 
                }
                else
                {
                        // the vma has already been registered.
                        ret = 0;
                        goto _out;
                }
	}
	
	vsi = alloc_vma_sksm_info();
        if(vsi)
        {
                ret = 1;
                goto _out;
        }
        
        vsi->vma = vma;
        
        // put the new vma to the red black tree.
        rb_link_node(&vsi->tree_node, parent, new);
        rb_insert_color(&vsi->tree_node, &vma_sksm_info_tree);
       
        // 并且放入零号vma链表尾
        list_add_tail(&vsi->level_list, &vma_sksm_info_heads[0]);
	
	spin_unlock(&vma_sksm_info_lock);
	
        return 0;
        
_out:        
        spin_unlock(&vma_sksm_info_lock);
        return ret;
}

int register_vma_sksm(struct vm_area_struct *vma)
{
        if(unlikely(is_mergeable_area(vma)))
        {
               return __register_vma_sksm(vma); 
        }
	return 0;
}

int __unregister_vma_sksm(struct vm_area_struct *vma)
{
        struct rb_node **new = &vma_sksm_info_tree.rb_node;
        struct rb_node *parent = NULL;
        struct vma_sksm_info *vsi;

        spin_lock(&vma_sksm_info_lock);
        
        while(*new)
        {
                vsi = rb_entry(*new, struct vma_sksm_info, tree_node);
                parent = *new;
                if(vma<vsi->vma)
                {
                       new = &parent->rb_left; 
                }
                else if(vma>vsi->vma)
                {
                       new = &parent->rb_right; 
                }
                else
                {
                        // found it~
                        rb_erase(&vsi->tree_node, &vma_sksm_info_tree);
                        free_vma_sksm_info(vsi);
			spin_unlock(&vma_sksm_info_lock);

                        return 0;
                }
        }
        
        spin_unlock(&vma_sksm_info_lock);
        
        return 1;
}

int unregister_vma_sksm(struct vm_area_struct *vma)
{
        if(unlikely(is_mergeable_area(vma)))
		return __unregister_vma_sksm(vma);
	return 0;
}


//// for sys filesystem.
#ifdef CONFIG_SYSFS
#define SKSM_ATTR_RO(_name) static struct kobj_attribute _name##_attr = __ATTR_RO(_name)
#define SKSM_ATTR(_name) static struct kobj_attribute _name##_attr = \
		__ATTR(_name, 0644, _name##_show, _name##_store)

static ssize_t sth_sksm_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "Hello, sksm. %u\n", vma_count);
}
SKSM_ATTR_RO(sth_sksm);

static ssize_t mytest_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	int count;
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	struct pid *pid = find_get_pid(the_pid);
	struct task_struct *task = pid_task(pid, PIDTYPE_PID);
	put_pid(pid);

	count = 0;
	if(!task)
	{
		count = sprintf(buf, "Failed to get pid %d's task_t.\n",
			the_pid);
		return count;
	}
	count = sprintf(buf, "Get pid %d's task_t OKAY.\n", the_pid);
	
	/*Fix me, Race condition here.*/
	mm = task->mm;
	down_read(&mm->mmap_sem);
	vma = mm->mmap;
	while(vma)
	{
		if(is_mergeable_area(vma))
			count += sprintf(buf+count, "%lx %lx\n", vma->vm_start,
				vma->vm_end);
		vma = vma->vm_next;
	}
	up_read(&mm->mmap_sem);

	return count;
}
static ssize_t mytest_store(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf, size_t count)
{
	//char *ptr = buf + count -1;
	the_pid = simple_strtol(buf, NULL, 10);
	return count;
	//buf[count] = 0;
	//printk(KERN_EMERG"%s ## %d\n", buf, count);
}
SKSM_ATTR(mytest);

static struct attribute *sksm_attrs[] = { 
	&sth_sksm_attr, 
	&mytest_attr,
	NULL 
};
static struct attribute_group sksm_attr_group = {
	.attrs = sksm_attrs,
	.name = "sksm", 
};

static int __init sksm_init(void)
{
    int err;
    vam_sksm_info_cache = SKSM_KMEM_CACHE(vma_sksm_info, 0);
    if (!vam_sksm_info_cache) {
	printk(KERN_DEBUG "SKSM: init sksm slab failed.\n");
    }
    err = init_vma_sksm_infos();
    if (err) {
	printk(KERN_DEBUG "SKSM: init_vma_sksm_infos failed. \n");
    }
    err = sysfs_create_group(mm_kobj, &sksm_attr_group);
    if (err) {
	printk(KERN_DEBUG "SKSM: register sysfs failed.\n");
    }
    printk("SKSM: [OKAY] calling SKSM_INIT.\n");
    return 0;
}

static void __exit sksm_exit(void)
{
    sysfs_remove_group(mm_kobj, &sksm_attr_group);
    kmem_cache_destroy(vam_sksm_info_cache);
    printk(KERN_DEBUG "SKSM: Exit.");
} 

module_init(sksm_init);
module_exit(sksm_exit);
#endif
