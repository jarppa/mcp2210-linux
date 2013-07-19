#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>

#include "mcp2210.h"

static int Device_Open = 0;

static char command_buf[MCP2210_BUFFER_SIZE];

static struct mcp2210_device *bound_device = NULL;

struct ctl_command {
    int id;
    int count;
    int sent;
    int response_count;
    int done;
};

static dev_t first; // Global variable for the first device number 
static struct cdev c_dev; // Global variable for the character device structure
static struct class *cl; // Global variable for the device class

int mcp2210_ctl_next_request(void *data, u8 *request) {
    struct ctl_command *cmd_data = data;
    
    printk("mcp2210_ctl_next_request() done: %d sent: %d count: %d\n", cmd_data->done, cmd_data->sent, cmd_data->count);

    if (cmd_data->done) { // Command done. Cleanup.
        kfree(cmd_data);
        return 0;
    }
    
    if (cmd_data->sent < MCP2210_BUFFER_SIZE) { // Transfer the data.
        memcpy(request, command_buf, MCP2210_BUFFER_SIZE);
        cmd_data->sent = MCP2210_BUFFER_SIZE;
        return 1;
    } else { // No response yet. Nothing to do.
        return 0;
    }
}

void mcp2210_ctl_data_received(void *data, u8 *response) {
    struct ctl_command *cmd_data = data;
    
    printk("mcp2210_ctl_data_received\n");
    cmd_data->done = 1; // Got our response. We're done.
}

void mcp2210_ctl_command_interrupted(void *data) {

    printk("mcp2210_ctl_command_interrupted\n");
}

static int my_open(struct inode *i, struct file *f)
{
    printk(KERN_INFO "Driver: open()\n");
  
    if (Device_Open)
        return -EBUSY;

    Device_Open++;
    //try_module_get(THIS_MODULE);
  
    return 0;
}

static int my_flush(struct file *f, fl_owner_t id)
{
    printk(KERN_INFO "Driver: flush()\n");
  
    //Device_Open--;
    //module_put(THIS_MODULE);
    
    return 0;
}

static int my_close(struct inode *i, struct file *f)
{
    printk(KERN_INFO "Driver: close()\n");
  
    Device_Open--;
    //module_put(THIS_MODULE);
    
    return 0;
}

static ssize_t my_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
    printk(KERN_INFO "Driver: read()\n");
    return 0;
}

static ssize_t my_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
    printk(KERN_INFO "Driver: write()\n");
    
    int i;
    int ret = 0;
    struct ctl_command *cmd_data;
  
    if (!access_ok(VERIFY_READ, buf, len))
        return 0;
  
    i = copy_from_user(command_buf, buf, MCP2210_BUFFER_SIZE);
    
    if ( (MCP2210_BUFFER_SIZE-i) < len )
        ret = MCP2210_BUFFER_SIZE-i;
    else
        ret = len;

    if (!bound_device) {
        printk(KERN_ALERT "No device bound. Ignoring writes!\n");
        return 0;
    }
    
    cmd_data = kzalloc(sizeof(struct ctl_command), GFP_KERNEL);
    if (!cmd_data)
        return -ENOMEM;
    
    cmd_data->id = 1;
    cmd_data->count = MCP2210_BUFFER_SIZE - i;
    cmd_data->sent = 0;
    cmd_data->response_count = 0;
    cmd_data->done = 0;
    
    printk(KERN_INFO "Got data from user space! len:%d count:%d cmd:%x\n", len, MCP2210_BUFFER_SIZE-i, command_buf[0]);
    
    if (mcp2210_add_command(bound_device, cmd_data, 
                            mcp2210_ctl_next_request, 
                            mcp2210_ctl_data_received, 
                            mcp2210_ctl_command_interrupted) != 0) {
        printk(KERN_ERR "No memory for HID data!\n");
    }

    return ret;
}

static struct file_operations mcp2210_fops =
{
  .owner = THIS_MODULE,
  .open = my_open,
  .release = my_close,
  .read = my_read,
  .write = my_write,
  .flush = my_flush
};


/* 
 * Initialize the module - Register the character device 
 */
int mcp2210_ctl_init_module(struct mcp2210_device *dev)
{
    int ret_val;
    /* 
     * Register the character device (atleast try) 
     */
    if (bound_device) {
        printk (KERN_INFO "Already bound to a device! Multiple devices cripled for now!\n");
        return 0;
    }
    
    bound_device = dev;
    
    if (alloc_chrdev_region(&first, 0, 1, "mcp2210_chardev") < 0) {
        return -1;
    }
    
    if ((cl = class_create(THIS_MODULE, "mcp2210_chardrv")) == NULL) {
        unregister_chrdev_region(first, 1);
        return -1;
    }
    
    if (device_create(cl, NULL, first, NULL, "mcp2210_ctl") == NULL) {
        class_destroy(cl);
        unregister_chrdev_region(first, 1);
        return -1;
    }
    
    cdev_init(&c_dev, &mcp2210_fops);
    
    if (cdev_add(&c_dev, first, 1) == -1) {
        device_destroy(cl, first);
        class_destroy(cl);
        unregister_chrdev_region(first, 1);
        return -1;
    }
    return 0;
    
}

/* 
 * Cleanup - unregister the appropriate file from /proc 
 */
void mcp2210_ctl_cleanup_module(struct mcp2210_device *dev)
{
    int ret;

    if (dev != bound_device) {
        printk(KERN_ALERT "Staph human! What are you doing?\n");
        return;
    }
    
    bound_device = NULL;
    
    cdev_del(&c_dev);
    device_destroy(cl, first);
    class_destroy(cl);
    unregister_chrdev_region(first, 1);
    printk(KERN_INFO "mcp2210_chrdrv unregistered\n");
}
