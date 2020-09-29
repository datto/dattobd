cd $BOX_DIR

# 'vagrant halt' hungs on Debian 8. Thus, just destroy. It's a bit brutal, but even faster...
vagrant destroy $INSTANCE_NAME --force && exit 0
echo "Failed to destroy box $INSTANCE_NAME by the 'vagrant destroy'."

# This should never happen, but as a "last-ditch foul"
# in the football, use virsh commands to find and remove lasts of the vagrant image. And then fail a build
# to notice this problem (get a red card and go away from the field, yeah).
res=0
# Name of the dir with the Vagrantfile is the prefix for the VM name.
dir=${BOX_DIR##*/}
if virsh list --all | grep -q ${dir}_${INSTANCE_NAME} ; then
    virsh destroy ${dir}_${INSTANCE_NAME}
    virsh undefine ${dir}_${INSTANCE_NAME}
    echo "The vagrant box ${dir}_${INSTANCE_NAME} was not removed by the 'vagrant destroy --force' and now removed by virsh."
    res=1
fi

if virsh vol-list default | grep -q ${dir}_${INSTANCE_NAME}.img ; then
    virsh vol-delete --pool default ${dir}_${INSTANCE_NAME}.img
    echo "${dir}_${INSTANCE_NAME}.img was not removed by the 'vagrant destroy --force' and now removed by virsh."
    res=2
fi

exit $res
