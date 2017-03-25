#include <linux/types.h> // dev_t
#include <linux/kdev_t.h> // Macros for dev_t
#include <linux/fs.h> // chrdev_, file_operations, file
#include <linux/module.h> // THIS_MODULE
#include <linux/moduleparam.h> // module_param, MODULE_PARM_DESC
#include <linux/cdev.h> // cdev, cdev_
#include <linux/kernel.h> // container_of, printk
#include <linux/slab.h> // kmalloc, kfree
#include <asm/uaccess.h> // copy_to/from_user
#include <linux/device.h> // device, class
#include <linux/list.h> // list_head

#define PROC_DEV_NAME "aspcdrvP"
#define DEV_NAME "mycdrv"
#define DEV_CLASS "aspcdrvC"
#define DEF_NUM_DEVICES 3
#define DEF_RAMDISK_SIZE (size_t) (16 * PAGE_SIZE)

#define CDRV_IOC_MAGIC 'Z'
#define ASP_CHGACCDIR _IOW(CDRV_IOC_MAGIC, 1, int)
#define REGULAR 0
#define REVERSE 1

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jaikrishna");
MODULE_DESCRIPTION("A simple character device driver");
MODULE_VERSION("1.0");

struct asp_cdrv {
	struct list_head list;
	struct cdev mycdev;
	char *ramdisk;
	struct mutex mut;
	unsigned int devNo;
	short accmode;
};

static int __init asp_init_module(void);
static void __exit asp_exit_module(void);
static void asp_cleanup_module(void);
static int asp_construct_device(int, struct class *);
static int asp_open(struct inode *, struct file *);
static int asp_release(struct inode *, struct file *);
static ssize_t asp_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t asp_write(struct file *, const char __user *, size_t, loff_t *);
static loff_t asp_llseek(struct file *, loff_t, int);
static long asp_ioctl(struct file *, unsigned int, unsigned long);

static struct file_operations asp_fops = {
	.open = asp_open,
	.release = asp_release,
	.read = asp_read,
	.write = asp_write,
	.llseek = asp_llseek,
	.unlocked_ioctl = asp_ioctl,
};

static unsigned int asp_major = 0;
static unsigned int NUM_DEVICES = DEF_NUM_DEVICES;
static unsigned int RAMDISK_SIZE = DEF_RAMDISK_SIZE;

module_param(NUM_DEVICES, int, S_IRUGO);
MODULE_PARM_DESC(NUM_DEVICES, "Number of devices to create");
module_param(RAMDISK_SIZE, int, S_IRUGO);
MODULE_PARM_DESC(RAMDISK_SIZE, "Size in bytes, for the ramdisk of each device");

static struct list_head asp_devlist;

static struct class *asp_class = NULL;

static int __init asp_init_module(void) {
	dev_t dev = 0;
	int rv = 0, i = 0;

	INIT_LIST_HEAD(&asp_devlist);

	if(NUM_DEVICES <= 0){
		printk(KERN_WARNING "%s: Number of devices %d is invalid\n", DEV_NAME, NUM_DEVICES);
		return -EINVAL;
	}
	
	rv = alloc_chrdev_region(&dev, 0, NUM_DEVICES, PROC_DEV_NAME);
	if(rv < 0) {
		printk(KERN_WARNING "%s: cannot allocate major number\n", DEV_NAME);
		return rv;
	}
	asp_major = MAJOR(dev);
	printk(KERN_INFO "%s: device allocated at device numbers %u:%u to %u:%u\n", DEV_NAME, asp_major, 0, asp_major, NUM_DEVICES - 1);

	asp_class = class_create(THIS_MODULE, DEV_CLASS);
	if (IS_ERR(asp_class)) {
		rv = PTR_ERR(asp_class);
		printk(KERN_WARNING "%s: error in creating device class\n", DEV_NAME);
		goto fail;
	}
	printk(KERN_INFO "%s: device class created successfully\n", DEV_NAME);

	for(i = 0; i < NUM_DEVICES ; i++) {
		rv = asp_construct_device(i, asp_class);
		if(rv) {
			printk(KERN_WARNING "%s: Error %d adding %s%d", DEV_NAME, rv, DEV_NAME, i);
			goto fail;
		}
	}

	printk(KERN_INFO "%s: Initialization succeeded\n", DEV_NAME);
	return 0;

fail:
	asp_cleanup_module();
	return rv;
}

static void asp_cleanup_module() {
	struct asp_cdrv *aspdev, *tmp;

	list_for_each_entry_safe(aspdev, tmp, &asp_devlist, list) {
		device_destroy(asp_class, MKDEV(asp_major, aspdev->devNo));
		cdev_del(&aspdev->mycdev);
		kfree(aspdev->ramdisk);
		mutex_destroy(&aspdev->mut);
		list_del(&aspdev->list);
		kfree(aspdev);
	}

	if (asp_class) {
		class_destroy(asp_class);
	}

	unregister_chrdev_region(MKDEV(asp_major, 0), NUM_DEVICES);
	printk(KERN_INFO "%s: module exiting\n", DEV_NAME);
	return;
}

static void __exit asp_exit_module(void) {
	asp_cleanup_module();
	return;
}

static int asp_construct_device(int devNo, struct class *clasp) {
	int rv = 0;
	struct asp_cdrv *aspdev = NULL;
	struct device *devp = NULL;

	aspdev = (struct asp_cdrv *) kzalloc(sizeof(struct asp_cdrv), GFP_KERNEL);
	if(!aspdev) {
		printk(KERN_WARNING "%s: allocating memory for device %u failed\n", DEV_NAME, devNo);
		rv = -ENOMEM;
		return rv;
	}

	aspdev->devNo = devNo;
	aspdev->accmode = 0;
	INIT_LIST_HEAD(&aspdev->list);
	list_add_tail(&aspdev->list, &asp_devlist);
	mutex_init(&aspdev->mut);

	aspdev->ramdisk = (char *) kzalloc(RAMDISK_SIZE * sizeof(char), GFP_KERNEL);
	if(!aspdev->ramdisk) {
		printk(KERN_WARNING "%s: allocating ramdisk for device %u failed\n", DEV_NAME, aspdev->devNo);
		rv = -ENOMEM;
		return rv;
	}

	cdev_init(&aspdev->mycdev, &asp_fops);
	aspdev->mycdev.owner = THIS_MODULE;
	aspdev->mycdev.ops = &asp_fops;

	rv = cdev_add(&aspdev->mycdev, MKDEV(asp_major, devNo), 1);
	if (rv) {
		printk(KERN_WARNING "%s: error in cdev_add for device %u\n", DEV_NAME, aspdev->devNo);
		return rv;
	}

	devp = device_create(clasp, NULL, MKDEV(asp_major, devNo), NULL, DEV_NAME "%d", devNo);
	if (IS_ERR(devp)) {
		rv = PTR_ERR(devp);
		printk(KERN_WARNING "%s: error in device_create for device %u\n", DEV_NAME, aspdev->devNo);
		cdev_del(&aspdev->mycdev);
		return rv;
	}

	return 0;
}

module_init(asp_init_module);
module_exit(asp_exit_module);

static int asp_open(struct inode *inodep, struct file *filp) {
	struct asp_cdrv *aspdev;
	unsigned int minor = iminor(inodep);

	if(asp_major == imajor(inodep) && minor >= 0 && minor < NUM_DEVICES) {
		list_for_each_entry(aspdev, &asp_devlist, list) {
			if(aspdev->devNo == minor)
				goto found;
		}
		printk(KERN_ERR "%s: Failed to find device %u:%u in list\n", DEV_NAME, asp_major, minor);
		return -ENODEV;
	} else {
		printk(KERN_ERR "%s: Failed to find device %u:%u\n", DEV_NAME, asp_major, minor);
		return -ENODEV;
	}

found:
	filp->private_data = aspdev;

	printk(KERN_INFO "%s: device %u has been opened\n", DEV_NAME, minor);
	return 0;
}

static int asp_release(struct inode *inodep, struct file *filp) {
	printk(KERN_INFO "%s: device %u closed\n", DEV_NAME, iminor(inodep));
	return 0;
}

static ssize_t asp_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos) {
	struct asp_cdrv *aspdev = (struct asp_cdrv *) filp->private_data;
	ssize_t rv = 0;
	unsigned int read_size;

	printk(KERN_INFO "%s: asp_read device %u, count = %ld, f_pos = %lld\n", DEV_NAME, aspdev->devNo, count, *f_pos);
	if(mutex_lock_killable(&aspdev->mut)) {
		printk(KERN_NOTICE "%s: asp_read not able to acquire mutex for device %u\n", DEV_NAME, aspdev->devNo);
		return -EINTR;
	}

	if(aspdev->accmode == 0) {
		if(*f_pos >= RAMDISK_SIZE || aspdev->ramdisk[*f_pos] == '\0' || *f_pos < 0)
			goto finish;

		read_size = strnlen(aspdev->ramdisk, RAMDISK_SIZE);
		if((*f_pos + (long long int) count) > read_size)
			count = read_size - *f_pos; // in this case, no NULL char

		printk(KERN_INFO "%s: asp_read device %u, final count = %ld, f_pos = %lld\n", DEV_NAME, aspdev->devNo, count, *f_pos);

		if(copy_to_user(buf, &(aspdev->ramdisk[*f_pos]), count)) {
			printk(KERN_WARNING "%s: copy_to_user failed\n", DEV_NAME);
			rv = -EFAULT;
			goto finish;
		}

		*f_pos += count;
		rv = count;
	} else {
		char *tempstr = NULL;
		int i;

		if(*f_pos >= RAMDISK_SIZE || *f_pos < 0)
			goto finish;
		if(aspdev->ramdisk[*f_pos] == '\0')
			goto finish;

		if((*f_pos - (long long int) count) < 1)
			count = *f_pos + 1;

		printk(KERN_INFO "%s: asp_read device %u, final count = %ld, f_pos = %lld\n", DEV_NAME, aspdev->devNo, count, *f_pos);

		tempstr = (char *) kzalloc(count + 1, GFP_KERNEL);
		if(!tempstr) {
			printk(KERN_WARNING "%s: Error allocating temporary memory\n", DEV_NAME);
			return -ENOMEM;
		}

		for(i = 0 ; i < count ; i++, *f_pos -= 1)
			tempstr[i] = aspdev->ramdisk[*f_pos];
		tempstr[i] = '\0';

		if(copy_to_user(buf, tempstr, count)) {
			printk(KERN_WARNING "%s: copy_to_user failed\n", DEV_NAME);
			rv = -EFAULT;
			goto finish;
		}
		kfree(tempstr);
		rv = count;
	}

finish:
	mutex_unlock(&aspdev->mut);
	printk(KERN_INFO "%s: asp_read for device %u complete\n", DEV_NAME, aspdev->devNo);
	return rv;
}

static ssize_t asp_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos) {
	struct asp_cdrv *aspdev = (struct asp_cdrv *) filp->private_data;
	ssize_t rv = 0;

	printk(KERN_INFO "%s: asp_write device %u, count = %ld, f_pos = %lld\n", DEV_NAME, aspdev->devNo, count, *f_pos);
	if(mutex_lock_killable(&aspdev->mut)) {
		printk(KERN_NOTICE "%s: asp_write cannot acquire mutex for device %u\n", DEV_NAME, aspdev->devNo);
		return -EINTR;
	}

	if(aspdev->accmode == 0) {
		if (*f_pos >= RAMDISK_SIZE || *f_pos < 0) {
			rv = -EINVAL;
			goto finish;
		}

		if ((*f_pos + (long long int) count) > RAMDISK_SIZE)
			count = RAMDISK_SIZE - *f_pos;

		printk(KERN_INFO "%s: asp_write device %u, final count = %ld, f_pos = %lld\n", DEV_NAME, aspdev->devNo, count, *f_pos);

		if(copy_from_user(&(aspdev->ramdisk[*f_pos]), buf, count)) {
			printk(KERN_WARNING "%s: copy_from_user failed\n", DEV_NAME);
			rv = -EFAULT;
			goto finish;
		}

		*f_pos += count;

		if(*f_pos < RAMDISK_SIZE)
			aspdev->ramdisk[*f_pos] = '\0'; // NULL character placed at end at all cases except when buffer is full

		rv = count;
	} else {
		char *tempstr = NULL;
		int i;

		if(*f_pos >= RAMDISK_SIZE || *f_pos < 0) {
			rv = -EINVAL;
			goto finish;
		}
		printk(KERN_INFO "%s: asp_write reverse %lld\n", DEV_NAME, *f_pos - count);

		if((*f_pos - (long long int) count) < 1)
			count = *f_pos + 1;

		printk(KERN_INFO "%s: asp_write device %u, final count = %ld, f_pos = %lld\n", DEV_NAME, aspdev->devNo, count, *f_pos);

		tempstr = (char *) kzalloc(count + 1, GFP_KERNEL);
		if(!tempstr) {
			printk(KERN_WARNING "%s: Error allocating temporary memory\n", DEV_NAME);
			return -ENOMEM;
		}

		if(copy_from_user(tempstr, buf, count)) {
			printk(KERN_WARNING "%s: copy_from_user failed\n", DEV_NAME);
			rv = -EFAULT;
			goto finish;
		}

		for(i = 0 ; i < count ; i++, *f_pos -= 1)
			aspdev->ramdisk[*f_pos] = tempstr[i];

		// no NULL character inserted in case of reverse write

		kfree(tempstr);
		rv = count;
	}

finish:
	mutex_unlock(&aspdev->mut);
	printk(KERN_INFO "%s: asp_write for device %u complete\n", DEV_NAME, aspdev->devNo);
	return rv;
}

static loff_t asp_llseek(struct file* filp, loff_t off, int whence) {
	struct asp_cdrv *aspdev = (struct asp_cdrv *) filp->private_data;
	loff_t newpos;
	printk(KERN_INFO "%s: asp_llseek device %u, f_pos = %lld, off = %lld, whence = %d", DEV_NAME, aspdev->devNo, filp->f_pos, off, whence);

	if(mutex_lock_killable(&aspdev->mut)) {
		printk(KERN_NOTICE "%s: asp_llseek cannot acquire mutex for device %u\n", DEV_NAME, aspdev->devNo);
		return -EINTR;
	}

	switch(whence) {
		case 0: // SEEK_SET
		newpos = off;
		break;

		case 1: // SEEK_CUR
		newpos = filp->f_pos + off;
		break;

		case 2: // SEEK_END
		newpos = RAMDISK_SIZE + off; // from end or /0
		break;

		default: // invalid argument
		mutex_unlock(&aspdev->mut);
		printk(KERN_WARNING "%s: asp_llseek has invalid argument for device %u\n", DEV_NAME, aspdev->devNo);
		return -EINVAL;
	}

	if (newpos < 0)
		newpos = 0;
	else if (newpos > RAMDISK_SIZE)
		newpos = RAMDISK_SIZE;

	filp->f_pos = newpos;
	mutex_unlock(&aspdev->mut);
	printk(KERN_INFO "%s: asp_llseek device %u seeked to newpos %llu\n", DEV_NAME, aspdev->devNo, newpos);
	return newpos;
}

static long asp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
	struct asp_cdrv *aspdev = (struct asp_cdrv *) filp->private_data;
	short prev_mode;
	int req_mode;

	if(cmd != ASP_CHGACCDIR)
		return -ENOTTY;

	if(copy_from_user(&req_mode, (int *) arg, sizeof(int)))
		return -EFAULT;

	if(req_mode != 0 && req_mode != 1)
		return -EINVAL;

	if(mutex_lock_killable(&aspdev->mut)) {
		printk(KERN_NOTICE "%s: asp_ioctl cannot acquire mutex for device %u\n", DEV_NAME, aspdev->devNo);
		return -EINTR;
	}

	prev_mode = aspdev->accmode;
	aspdev->accmode = (short) req_mode;
	mutex_unlock(&aspdev->mut);
	printk(KERN_INFO "%s: asp_ioctl changed direction of device %u to %s\n", DEV_NAME, aspdev->devNo, arg?"reverse":"regular");
	return ((long) prev_mode);
}
