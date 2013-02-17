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
#include <linux/pid.h>
#include <linux/cryptohash.h>
#include <crypto/hash.h>

MODULE_LICENSE("GPL");

#define KSM_KMEM_CACHE(__struct, __flags) kmem_cache_create("ksm_"#__struct, \
	sizeof(struct __struct), __alignof__(struct __struct), (__flags), NULL);

static struct kmem_cache *bitmap_cache;

struct bitmap{
	unsigned char *mem;
	int length;
};
// Every pair of "two-bit" correspond to a page or page frame.
// size specifies at least how many pairs of 'two-bit' will be allocated.
struct bitmap *bitmap_create(unsigned int page_count)
{
	struct bitmap *bitmap;
	int size_in_byte = ( page_count + 3) / 4;
	unsigned char *mem = kmalloc(size_in_byte, GFP_KERNEL);
	if (NULL==mem)
		return NULL;
	memset(mem, 0, size_in_byte);
	bitmap = kmem_cache_alloc(bitmap_cache, GFP_KERNEL);
	if (NULL==bitmap)
		return NULL;
	bitmap->mem = mem;
	bitmap->length = size_in_byte;
	
	return bitmap;	
}

void bitmap_destroy(struct bitmap *bitmap)
{
	BUG_ON(NULL==bitmap);
	kfree(bitmap->mem);
	kmem_cache_free(bitmap_cache, bitmap);
}

/**
 * @index the index number of two-bit pair starting from 0;
 * @ch only 0,1,2,3 are valid.
 * This function will return -1 if failed, otherwise return
 * 0 on success.
 */
int bitmap_write(struct bitmap *bm, unsigned index, u8 ch)
{
	u8 needle;
	unsigned int pos;
	pos = ( index*2 + 7 ) / 8;
printk(KERN_DEBUG"bitmap_write: %d %d\n", index, ch);
	if (pos >= bm->length) 
		return -1;
	needle = index*2 % 8;	
	ch &= 0x03;
	ch <<= needle;
	bm->mem[pos] &= ~(0x3<<needle);
	bm->mem[pos] |= ch; 

	return 0;
}

int bitmap_read(struct bitmap *bm, unsigned index)
{
	u8 val, needle;
	unsigned int pos;
	pos = ( index*2 + 7 ) / 8;
printk(KERN_DEBUG"bitmap_read: %d\n", index);
	if (pos >= bm->length)
		return -1;

	needle = index*2 % 8;	
	val = bm->mem[pos];
	val >>= needle;
	val &= 0x03;
printk(KERN_DEBUG"bitmap_read got %d\n", val);
	return val;
}

//// for sys filesystem.
#ifdef CONFIG_SYSFS
#define KSM_ATTR_RO(_name) static struct kobj_attribute _name##_attr = __ATTR_RO(_name)
#define KSM_ATTR(_name) static struct kobj_attribute _name##_attr = \
		__ATTR(_name, 0644, _name##_show, _name##_store)

static struct bitmap *my_bitmap;
static unsigned int my_index;
static ssize_t sth_ksm_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "Hello, XXXXXXXXXXX.");
}
static ssize_t sth_ksm_store(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf, size_t count)
{
	my_index = simple_strtol(buf, NULL, 10);
	return count;
}
KSM_ATTR(sth_ksm);

static ssize_t mytest_show(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	unsigned char ch = bitmap_read(my_bitmap, my_index);
	return sprintf(buf, "%d\n", ch);
}
static ssize_t mytest_store(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf, size_t count)
{
	char *p;
	unsigned int index = simple_strtol(buf, &p, 10);	
	unsigned char ch = simple_strtol(++p, NULL, 10);
	bitmap_write(my_bitmap, index, ch);
	
	return count;
}
KSM_ATTR(mytest);

static struct attribute *sksm_attrs[] = { 
	&sth_ksm_attr, 
	&mytest_attr,
	NULL 
};
static struct attribute_group ksm_attr_group = {
	.attrs = sksm_attrs,
	.name = "fable", 
};

char* bins2hexs(const unsigned char* mem, int mem_len, char* buff, int buff_len)
{
	int len_expected, i;
	char *buff_expected;
	char small_buff[3];

	if ( NULL == mem || 0 == mem_len || NULL == buff || 0 == buff_len ) 
	{
		if ( NULL != buff && 0 != buff_len )
		{
			memset(buff, 0, buff_len);
			return buff;
		}
		else
		{
			return NULL;
		}
	}

	memset(buff, 0, buff_len);

	len_expected = mem_len * 2;
	//char* buff_expected = new char[len_expected];
	buff_expected = kmalloc(len_expected, GFP_KERNEL);
	for ( i = 0; i < mem_len; i ++ )
	{
		int height = ( (mem[i] & 0xf0) >> 4);
		int low = (mem[i] & 0x0f);

		sprintf(small_buff, "%X", height);
		sprintf(small_buff+1, "%X", low);
		
		memcpy(buff_expected + i * 2, small_buff, 2);
	}
	
	if ( buff_len >= len_expected )
	{
		memcpy(buff, buff_expected, len_expected);
	}

	//delete []buff_expected;
	kfree(buff_expected);

	return buff;
}

int md5_hash(const char *mem, u32 len, u8 *hash)
{
	u32 size;
	struct shash_desc *sdescmd5;
	int err;

        struct crypto_shash *md5 = crypto_alloc_shash("md5", 0, 0);
      if (IS_ERR(md5)) 
			return -1;
      size = sizeof(struct shash_desc) + crypto_shash_descsize(md5);
      sdescmd5 = kmalloc(size, GFP_KERNEL);
      if (!sdescmd5) {
              err = -1;
              goto malloc_err;
      }
      sdescmd5->tfm = md5;
      sdescmd5->flags = 0x0;

      err = crypto_shash_init(sdescmd5);
      if (err) {
				err = -1;
              goto hash_err;
      }
      crypto_shash_update(sdescmd5, mem, len);
      err = crypto_shash_final(sdescmd5, hash);

hash_err:
      kfree(sdescmd5);
malloc_err:
      crypto_free_shash(md5);

      return err;
}

static int __init ksm_init(void)
{
	int err;
	char buf[512];
	char buf2[512];
	char* hello = "hello";
	md5_hash(hello, 5, buf);
	printk( "ksm_init: %s\n", bins2hexs(buf, 16, buf2, 512));
	
return 0;
	bitmap_cache = KSM_KMEM_CACHE(bitmap, 0);
	if (NULL == bitmap_cache){
		printk(KERN_EMERG"Failed to create bitmap_cache.\n");
		return 1;
	}

	err = sysfs_create_group(mm_kobj, &ksm_attr_group);
	if (err) {
		printk(KERN_EMERG"ksm: register sysfs failed.\n");
		return 2;
	}

	my_bitmap = bitmap_create(25);
	if (!my_bitmap){
		printk(KERN_EMERG"ksm: bitmap_create failed.\n");
	}

	return 0;
}

static void __exit ksm_exit(void)
{return;
	bitmap_destroy(my_bitmap);
	sysfs_remove_group(mm_kobj, &ksm_attr_group);
	kmem_cache_destroy(bitmap_cache);
} 

module_init(ksm_init);
module_exit(ksm_exit);
#endif

