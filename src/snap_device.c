#include "includes.h"
#include "snap_device.h"
#include "module_control.h"
#include "tracer.h"
#include "logging.h"
#include "tracer_helper.h"

static struct snap_device** snap_devices;
static struct mutex snap_device_lock;

/**
 * init_snap_device_array() - Allocates the global device array.
 */
int init_snap_device_array(void){
    LOG_DEBUG("allocate global device array");
    snap_devices =
            kzalloc(dattobd_max_snap_devices * sizeof(struct snap_device *),
                    GFP_KERNEL);
    if (!snap_devices) {
            return -ENOMEM;
    }
    mutex_init(&snap_device_lock);
    return 0;
}

/**
 * cleanup_snap_device_array() - Frees the global device array.
 */
void cleanup_snap_device_array(void){
    LOG_DEBUG("destroying snap devices");
    if (snap_devices) {
            int i;
            struct snap_device *dev;

            snap_device_array_mut snap_devices_wrp = get_snap_device_array_mut();

            tracer_for_each(dev, i)
            {
                    if (dev) {
                            LOG_DEBUG("destroying minor - %d", i);
                            tracer_destroy(dev, snap_devices_wrp);
                    }
            }
        
            put_snap_device_array_mut(snap_devices_wrp);
            kfree(snap_devices);
            snap_devices = NULL;
    }
}


/**
 * get_snap_device_array() - Retrieves the immutable global device array.
 *
 * Return: The immutable global device array.
 */
snap_device_array get_snap_device_array(void){
    mutex_lock(&snap_device_lock);
    return snap_devices;
}

/**
 * get_snap_device_array_mut() - Retrieves the mutable global device array.
 * 
 * Return: The mutable global device array.
 */
snap_device_array_mut get_snap_device_array_mut(void){
    mutex_lock(&snap_device_lock);
    return snap_devices;
}

/**
 * get_snap_device_array_nolock() - Retrieves the immutable global device array without locking.
 *
 * Return: The global device array.
 */
snap_device_array get_snap_device_array_nolock(void){
    return snap_devices;
}

/**
 * put_snap_device_array() - Releases the immutable global device array.
 * 
 * @snap_devices: The immutable global device array.
 */
void put_snap_device_array(snap_device_array snap_devices){
    mutex_unlock(&snap_device_lock);
    return;
}


/**
 * put_snap_device_array_mut() - Releases the mutable global device array.
 * 
 * @snap_devices: The mutable global device array.
 */
void put_snap_device_array_mut(snap_device_array_mut snap_devices){
    mutex_unlock(&snap_device_lock);
    return;
}

/**
 * put_snap_device_array_nolock() - Releases the immutable global device array without locking.
 * 
 * @snap_devices: The global device array.
 */
void put_snap_device_array_nolock(snap_device_array snap_devices){
    return;
}
