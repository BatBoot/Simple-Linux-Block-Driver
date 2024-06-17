#include "device_file.h"
#include <linux/fs.h> 	    /* file lib */
#include <linux/kernel.h>   /* printk() */
#include <linux/errno.h>    /* error codes */
#include <linux/module.h>   /* THIS_MODULE */
#include <linux/uaccess.h>  /* copy_to_user() */
#include <linux/blkdev.h>   /* block device lib */ 
#include <linux/genhd.h>    /* individual disk device lib*/
#include <linux/hdreg.h>    /* disk drive lib*/
#include <linux/types.h>    /* size_t */
#include <linux/vmalloc.h>  /* vmalloc lib*/
#include <linux/slab.h>     /* kernel allocation lib*/
#include <linux/blk-mq.h>   /* multi-queue lib*/
#include <linux/buffer_head.h> /* buffer head lib*/

static const int deviceCap = 1024;//Number of sectors
static const int sectorCap = 512; //Sector
static const char deviceName[] = "IliesB-Driver"; //Device name
static int deviceMajor;           //Device number


static struct simple_block_device_t
{
	size_t size;                     //size of the entire driver
	spinlock_t lock;				 //Mutex for mutual exclusion
	uint8_t* data;					 //Actual data
	struct blk_mq_tag_set tag;       //Device queue tag
	struct request_queue* reqQueue;  //Queue for user requests
	struct gendisk* gd;			     //Memory disks
};

static struct simple_block_device_t* myDevice;
/******************************************************************************/
static int simple_block_driver_open(struct block_device* dev, fmode_t mode)
{
	printk(KERN_NOTICE "IliesBlock-Driver: Block device opened!\n");
	return 0;
}
/******************************************************************************/
static void simple_block_driver_release(struct gendisk* gd, fmode_t mode)
{
	printk(KERN_NOTICE "IliesBlock-Driver: Block device released!\n");
}
/******************************************************************************/
static int simple_block_driver_ioctl(struct block_device* dev, fmode_t mode,
									 unsigned int cmd, size_t arg)
{
	printk(KERN_NOTICE "IliesBlock-Driver: Block device ioctl command: 0x%08x\n", cmd);
	return 0;
}
/******************************************************************************/

static int simple_block_execute_request(struct request* req, size_t* bytes_num)
{
	struct bio_vec vec;                                
	struct req_iterator itr;                                //Queue iterator
	struct simple_block_device_t* dev = req->q->queuedata;  //Accesses device
	loff_t pos = blk_rq_pos(req) << SECTOR_SHIFT;			//Get the position of the current secotr
	loff_t device_size = (loff_t)(dev->size << SECTOR_SHIFT); //Get the size of the sector
	
	printk(KERN_NOTICE "IliesBlock-Driver: Request start from pos = %lld, device size = %lld", pos, device_size);
	
	
	//Special iteration function for all the requests
	rq_for_each_segment(vec, req, itr)
	{
		size_t vec_size = vec.bv_len;
		
		//Get the pointer to the page
		void* vec_buf = page_address(vec.bv_page) + vec.bv_offset;
		
		if((pos + vec_size) > device_size) vec_size = (size_t) (device_size - pos);
		
		//Check if operations is Write or Read
		if(rq_data_dir(req) == WRITE) memcpy(dev->data + pos, vec_buf, vec_size);
		else memcpy(vec_buf, dev->data + pos, vec_size);
		
		
		//Increment the counters
		pos += vec_size;
		*bytes_num += vec_size;
		
	}
	
	return 0;
}
/******************************************************************************/

static blk_status_t simple_block_device_request_handle(struct blk_mq_hw_ctx* hard_queue, const struct blk_mq_queue_data* data)
{
	size_t bytes_num = 0;
	struct request* req = data->rq;
	blk_status_t result = BLK_STS_OK;
	
	blk_mq_start_request(req); //start pulling queue requests
	
	//Execute the request and check for any erros returned
	if(simple_block_execute_request(req, &bytes_num)) 
		result = BLK_STS_IOERR; 
	
	//Notify kernel about the number of bytes processes when updating the queue request 
	if(blk_update_request(req, result, bytes_num)) BUG();
	
	//Stop processing the queue requests
	__blk_mq_end_request(req, result);
	
	return result;
}

/******************************************************************************/

// Define device block operations
static struct block_device_operations simple_block_fops = 
{
	.owner = THIS_MODULE,
	.open = simple_block_driver_open,
	.release = simple_block_driver_release,
	.ioctl = simple_block_driver_ioctl
};

// Define request queue operations
static struct blk_mq_ops queue_fops = 
{
	.queue_rq = simple_block_device_request_handle,
};



int simple_block_register(void)
{
	printk(KERN_NOTICE "IliesBlock-Driver: Initilizing the block device with disk size %d and sector size %d\n", deviceCap, sectorCap);
	
    //Register device
    deviceMajor = register_blkdev(0, deviceName);  //Let OS decide on a Major number
	
	if(deviceMajor < 0)
	{
		printk(KERN_WARNING "IliesBlock-Driver: Failed to register block device!\n");
		return -EBUSY;
	}
	
	printk(KERN_NOTICE "IliesBlock-Driver: Device register with major number: %d!\n", deviceMajor);
	
	myDevice = kmalloc(sizeof(struct simple_block_device_t), GFP_KERNEL); //allocate block memory for the whole device driver
	
	if(myDevice == NULL)
	{
		printk(KERN_WARNING "IliesBlock-Driver: Could not allocate memory for the device driver!\n");
		unregister_blkdev(deviceMajor, deviceName);
		return -ENOMEM;
	}
	
	//Initiialize the device
	myDevice->size = (deviceCap * sectorCap);     //set the size of the entire device
	spin_lock_init(&myDevice->lock);            //initialize semaphore
	
	myDevice->data = kmalloc(myDevice->size, GFP_KERNEL); //allocate actual data memory
	
	if(myDevice->data == NULL) 
	{
		printk(KERN_WARNING "IliesBlock-Driver: Failed to allocate memory for the block device!\n");
		unregister_blkdev(deviceMajor, deviceName);
		return -ENOMEM;
	}
	
	printk(KERN_NOTICE "IliesBlock-Driver: Initilizing driver request queue");
	
	//Initilize request queue
	myDevice->reqQueue = blk_mq_init_sq_queue(&myDevice->tag, &queue_fops, 128, BLK_MQ_F_SHOULD_MERGE);
	
	if(myDevice->reqQueue == NULL)
	{
		printk(KERN_WARNING "IliesBlock-Driver: Failed to allocate memory for the request queue!\n");
		kfree(myDevice->data);
		unregister_blkdev(deviceMajor, deviceName);
		return -ENOMEM;
	}
	
	myDevice->reqQueue->queuedata = myDevice; //Set the driver itself as the request data
	

	myDevice->gd = alloc_disk(1); 				 //Allocate a new disk
	myDevice->gd->flags = GENHD_FL_NO_PART_SCAN; //Do no scan for partitions
    myDevice->gd->major = deviceMajor;			 //Set the disk major to the device major
    myDevice->gd->first_minor = 0;               
    myDevice->gd->fops = &simple_block_fops;     //Set the disk operations
    myDevice->gd->private_data = &myDevice;      //Set the private data as the device itself
    
    strcpy(myDevice->gd->disk_name, "IliesBlock_sbd0");
    
    //Set the capacity of the disk
    set_capacity(myDevice->gd, myDevice->size);
    
    myDevice->gd->queue = myDevice->reqQueue;
    add_disk(myDevice->gd);					    //Add the new disk
	printk(KERN_NOTICE "IliesBlock-Driver: Block device fully configured!\n");
	
	return 0;
}

/******************************************************************************/
void simple_block_unregister(void)
{
	if(myDevice->gd)
	{
		del_gendisk(myDevice->gd);                //Delete disk partition(s)
		put_disk(myDevice->gd);                   //Free gendisk allocated memory
    }
    
    //Clean the queue request
    if(myDevice->reqQueue) blk_cleanup_queue(myDevice->reqQueue);	
    
          
	kfree(myDevice->data);                        //Free the memory allocated for the memory
	
	unregister_blkdev(deviceMajor, deviceName);   //Unregister the block device driver
	kfree(myDevice);						      //Free the memory allocated for the driver
	printk(KERN_NOTICE "IliesBlock-Driver: Block device successfully unregistered!\n");
	
}

/******************************************************************************/
