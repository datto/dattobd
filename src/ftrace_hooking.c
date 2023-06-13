#include "ftrace_hooking.h"
#include <linux/kprobes.h>

int (*orig_path_mount)(const char *dev_name, struct path *path,
		const char *type_page, unsigned long flags, void *data_page);

int ftrace_path_mount(const char *dev_name, struct path *path,
		const char *type_page, unsigned long flags, void *data_page)

{
	int ret = 0;
        int sys_ret = 0;
        unsigned int idx = 0;
        unsigned long real_flags = flags;
        char *dir_name = NULL;
        char *buf = NULL;

        buf = kmalloc(PATH_MAX, GFP_KERNEL);
        if (!buf) {
                return -ENOMEM;
        }

        // get rid of the magic value if its present 
        if ((real_flags & MS_MGC_MSK) == MS_MGC_VAL)
                real_flags &= ~MS_MGC_MSK;

   	if (real_flags & (MS_BIND | MS_SHARED | MS_PRIVATE | MS_SLAVE |
                          MS_UNBINDABLE | MS_MOVE) ||
                ((real_flags & MS_RDONLY) && !(real_flags & MS_REMOUNT))) {
                // bind, shared, move, or new read-only mounts it do not affect
                // the state of the driver
  
                sys_ret = orig_path_mount(dev_name, path, type_page, flags, data_page);
        } else if ((real_flags & MS_RDONLY) && (real_flags & MS_REMOUNT)) {
                // we are remounting read-only, same as umounting as far as the
                // driver is concerned
                dir_name = d_path(path, buf, PATH_MAX);
                ret = handle_bdev_mount_nowrite(dir_name, 0, &idx);

                sys_ret = orig_path_mount(dev_name, path, type_page, flags, data_page);
                post_umount_check(ret, sys_ret, idx, dir_name);
        } else {
                // new read-write mount
                sys_ret = orig_path_mount(dev_name, path, type_page, flags, data_page);

                if (!sys_ret) {
                        dir_name = d_path(path, buf, PATH_MAX);    
                        ret = handle_bdev_mounted_writable(dir_name, &idx);
                }
        }

        if(buf)
                kfree(buf);
        return sys_ret;
}

long (*orig_do_mount)(const char *dev_name, const char __user *dir_name,
		const char *type_page, unsigned long flags, void *data_page);

long ftrace_do_mount(const char *dev_name, const char __user *dir_name,
		const char *type_page, unsigned long flags, void *data_page)
{
	long ret = 0;
        long sys_ret;
        unsigned int idx = 0;
        unsigned long real_flags = flags;
           
        // get rid of the magic value if its present
        if ((real_flags & MS_MGC_MSK) == MS_MGC_VAL)
                real_flags &= ~MS_MGC_MSK;

        if (real_flags & (MS_BIND | MS_SHARED | MS_PRIVATE | MS_SLAVE |
                          MS_UNBINDABLE | MS_MOVE) ||
            ((real_flags & MS_RDONLY) && !(real_flags & MS_REMOUNT))) {
                // bind, shared, move, or new read-only mounts it do not affect
                // the state of the driver
                sys_ret = orig_do_mount(dev_name, dir_name, type_page, flags, data_page);
        } else if ((real_flags & MS_RDONLY) && (real_flags & MS_REMOUNT)) {
                // we are remounting read-only, same as umounting as far as the
                // driver is concerned

                ret = handle_bdev_mount_nowrite(dir_name, 0, &idx);                
                sys_ret = orig_do_mount(dev_name, dir_name, type_page, flags, data_page);
                post_umount_check(ret, sys_ret, idx, dir_name);
        } else {
                // new read-write mount
                sys_ret = orig_do_mount(dev_name, dir_name, type_page, flags, data_page);
                if (!sys_ret) {
                        handle_bdev_mounted_writable(dir_name, &idx);
                }
        }

        return sys_ret;
}

int (*orig_ksys_mount)(char __user *dev_name, char __user *dir_name, char __user *type,
	       unsigned long flags, void __user *data);

int ftrace_ksys_mount(char __user *dev_name, char __user *dir_name, char __user *type,
	       unsigned long flags, void __user *data)
{
	long ret = 0;
        long sys_ret = 0;
        unsigned int idx = 0;
        unsigned long real_flags = flags;
 
        // get rid of the magic value if its present
        if ((real_flags & MS_MGC_MSK) == MS_MGC_VAL)
                real_flags &= ~MS_MGC_MSK;

        if (real_flags & (MS_BIND | MS_SHARED | MS_PRIVATE | MS_SLAVE |
                          MS_UNBINDABLE | MS_MOVE) ||
            ((real_flags & MS_RDONLY) && !(real_flags & MS_REMOUNT))) {
                // bind, shared, move, or new read-only mounts it do not affect
                // the state of the driver
                sys_ret = orig_ksys_mount(dev_name, dir_name, type, flags, data);
        } else if ((real_flags & MS_RDONLY) && (real_flags & MS_REMOUNT)) {
                // we are remounting read-only, same as umounting as far as the
                // driver is concerned
                ret = handle_bdev_mount_nowrite(dir_name, 0, &idx);
                sys_ret = orig_ksys_mount(dev_name, dir_name, type, flags, data);
                post_umount_check(ret, sys_ret, idx, dir_name);
        } else {
                // new read-write mount
                sys_ret = orig_ksys_mount(dev_name, dir_name, type, flags, data);
                if (!sys_ret)
                        handle_bdev_mounted_writable(dir_name, &idx);
        }

        return sys_ret;
}

asmlinkage long (*orig_sys_mount)(char __user *dev_name, char __user *dir_name,
				char __user *type, unsigned long flags,
				void __user *data);

asmlinkage long ftrace_sys_mount(char __user *dev_name, char __user *dir_name,
				char __user *type, unsigned long flags,
				void __user *data)
{
        int ret = 0;
        long sys_ret = 0;
        unsigned int idx = 0;
        unsigned long real_flags = flags;

        // get rid of the magic value if its present
        if ((real_flags & MS_MGC_MSK) == MS_MGC_VAL)
                real_flags &= ~MS_MGC_MSK;

        if (real_flags & (MS_BIND | MS_SHARED | MS_PRIVATE | MS_SLAVE |
                          MS_UNBINDABLE | MS_MOVE) ||
            ((real_flags & MS_RDONLY) && !(real_flags & MS_REMOUNT))) {
                // bind, shared, move, or new read-only mounts it do not affect
                // the state of the driver
                sys_ret = orig_sys_mount(dev_name, dir_name, type, flags, data);
        } else if ((real_flags & MS_RDONLY) && (real_flags & MS_REMOUNT)) {
                // we are remounting read-only, same as umounting as far as the
                // driver is concerned
                ret = handle_bdev_mount_nowrite(dir_name, 0, &idx);
                sys_ret = orig_sys_mount(dev_name, dir_name, type, flags, data);
                post_umount_check(ret, sys_ret, idx, dir_name);
        } else {
                // new read-write mount
                sys_ret = orig_sys_mount(dev_name, dir_name, type, flags, data);
                if (!sys_ret)
                        handle_bdev_mounted_writable(dir_name, &idx);
        }

        return sys_ret;
}

int (*orig_path_umount)(struct path *path, int flags);

int ftrace_path_umount(struct path *path, int flags)
{
      	int ret = 0;
        int sys_ret = 0;
        unsigned int idx = 0;
        char *dir_name = NULL;
        char *buf;
        int real_flags = flags;

        // get rid of the magic value if its present
        if ((real_flags & MS_MGC_MSK) == MS_MGC_VAL)
		real_flags &= ~MS_MGC_MSK;

        buf = kmalloc(PATH_MAX, GFP_KERNEL);
        if (!buf) {
                return -ENOMEM;
        }
 
        dir_name = d_path(path, buf, PATH_MAX);
        ret = handle_bdev_mount_nowrite(dir_name, real_flags, &idx);

        sys_ret = orig_path_umount(path, flags);
        
        dir_name = d_path(path, buf, PATH_MAX);
        post_umount_check(ret, sys_ret, idx, dir_name);

        if(buf)
                kfree(buf);

        return sys_ret;
}

static int (*orig_ksys_umount)(char __user *name, int flags);

static int ftrace_ksys_umount(char __user *name, int flags)
{
	int ret = 0;
        int sys_ret = 0;
        unsigned int idx = 0;
        
        ret = handle_bdev_mount_nowrite(name, flags, &idx);

        sys_ret = orig_ksys_umount(name, flags);
        post_umount_check(ret, sys_ret, idx, name);

        return sys_ret;
}

asmlinkage long (*orig_sys_umount)(char __user *name, int flags);

asmlinkage long ftrace_sys_umount(char __user *name, int flags)
{
        int ret = 0;
        long sys_ret = 0;
        unsigned int idx = 0;

        ret = handle_bdev_mount_nowrite(name, flags, &idx);
        sys_ret = orig_sys_umount(name, flags);
        post_umount_check(ret, sys_ret, idx, name);

        return sys_ret;
}

#ifdef HAVE_SYS_OLDUMOUNT
asmlinkage long (*orig_sys_oldumount)(char __user *name);

asmlinkage long ftrace_sys_oldumount(char __user *name)
{
        int ret;
        long sys_ret;
        unsigned int idx;
       
        ret = handle_bdev_mount_nowrite(name, 0, &idx);
        sys_ret = orig_sys_oldumount(name);
        post_umount_check(ret, sys_ret, idx, name);

        return sys_ret;
}
#endif //HAVE_SYS_OLDUMOUNT

static struct ftrace_hook ftrace_hooks[] = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,9,0)
        HOOK("path_mount", ftrace_path_mount, &orig_path_mount),
        HOOK("path_umount", ftrace_path_umount, &orig_path_umount),
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
        //HOOK("do_mount", ftrace_do_mount, &orig_do_mount),
	//HOOK("ksys_umount", ftrace_ksys_umount, &orig_ksys_umount),
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
	HOOK("ksys_mount", ftrace_ksys_mount, &orig_ksys_mount),
	HOOK("ksys_umount", ftrace_ksys_umount, &orig_ksys_umount),
#else
	HOOK("sys_mount", ftrace_sys_mount, &orig_sys_mount),
	HOOK("sys_umount", ftrace_sys_umount, &orig_sys_umount),
#ifdef HAVE_SYS_OLDUMOUNT
        HOOK("sys_oldumount", ftrace_sys_oldumount, &orig_sys_oldumount),
#endif //HAVE_SYS_OLDUMOUNT
#endif //LINUX_VERSION_CODE
};

// Needs CONFIG_KPROBES=y as well as CONFIG_KALLSYMS=y
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
static unsigned long lookup_name(const char *name)
{
	struct kprobe kp = {
		.symbol_name = name
	};
	unsigned long address = 0;
	int ret = 0;

	ret = register_kprobe(&kp);

	if (ret < 0) 
	{
		LOG_ERROR(ret, "failed registering kprobe for %s", name);
		return 0;
	}
	address = (unsigned long)kp.addr;
	unregister_kprobe(&kp);

	return address;
}
#else
static unsigned long lookup_name(const char *name)
{
	unsigned long address;
	address = kallsyms_lookup_name(name);

	return address;
}
#endif //LINUX_VERSION_CODE

static int resolve_hook_address(struct ftrace_hook *hook)
{
	hook->address = lookup_name(hook->name);

	if (!hook->address) {
		LOG_ERROR(-ENOENT, "unresolved symbol: %s", hook->name);
		return -ENOENT;
	}

#if USE_FENTRY_OFFSET
	*((unsigned long*) hook->original) = hook->address + MCOUNT_INSN_SIZE;
#else
	*((unsigned long*) hook->original) = hook->address;
#endif //USE_FENTRY_OFFSET

	return 0;
}

static void notrace ftrace_callback_handler(unsigned long ip, unsigned long parent_ip,
		struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);
	struct ftrace_hook *hook = container_of(ops, struct ftrace_hook, ops);

#if USE_FENTRY_OFFSET
	regs->ip = (unsigned long)hook->function;
#else
	if (!within_module(parent_ip, THIS_MODULE))
		regs->ip = (unsigned long)hook->function;
#endif //USE_FENTRY_OFFSET
}

/**
 * register_hook() - registers and enables a single hook
 * @hook: a hook to install
 * 
 * Return:
 * 0 - success
 * !0 - an errno indicating the error
 */
int register_hook(struct ftrace_hook *hook)
{
	int ret = 0;

	ret = resolve_hook_address(hook);
	if (ret)
	{
		LOG_ERROR(ret, "failed resolving hook address for %s", hook->name);
		return ret;
	}
	
	hook->ops.func = ftrace_callback_handler;
	hook->ops.flags = FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_RECURSION | FTRACE_OPS_FL_IPMODIFY;

	ret = ftrace_set_filter_ip(&hook->ops, hook->address, 0, 0);
	if (ret) {
		LOG_ERROR(ret, "failed setting ftrace filter ip: %d for %s", ret, hook->name);
		return ret;
	}

	ret = register_ftrace_function(&hook->ops);
	if (ret) {
		LOG_ERROR(ret, "failed registering ftrace function for %s", hook->name);
		ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
		return ret;
	}

        LOG_DEBUG("registered ftrace hook for %s", hook->name);

	return ret;
}

/**
 * unregister_hook() - disable and unregister a single hook
 * @hook: a hook to remove
 * 
 * Return:
 * 0 - success
 * !0 - an errno indicating the error
 */
int unregister_hook(struct ftrace_hook *hook)
{
	int ret = 0;

	ret = unregister_ftrace_function(&hook->ops);
	if (ret) {
		LOG_ERROR(ret, "failed unregistering ftrace function for %s", hook->name);
	}

	ret = ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
	if (ret) {
		LOG_ERROR(ret, "failed setting ftrace filter ip for %s", hook->name);
	}
        return ret;
}

int register_ftrace_hooks(void)
{
	int ret = 0;
	int i;
	int count = ARRAY_SIZE(ftrace_hooks);

	for (i = 0; i < count; i++) {
		ret = register_hook(&ftrace_hooks[i]);
		if (ret)
			goto error;
	}

	return 0;
error:
	while (i != 0) {
		unregister_hook(&ftrace_hooks[--i]);
	}
	return ret;
}

int unregister_ftrace_hooks(void)
{
	int ret = 0;
	int i;
	int count = ARRAY_SIZE(ftrace_hooks);

	for (i = 0; i < count; i++) {
		unregister_hook(&ftrace_hooks[i]);
	}

	return ret;
}
