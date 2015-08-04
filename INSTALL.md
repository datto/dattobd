# dattobd INSTALL

## From Repositories
We recommend that you install the kernel module from Datto's repositories. Datto provides repos for RHEL/CentOS, Fedora, and Ubuntu.

### Ubuntu
```bash
sudo apt-key adv --recv-keys --keyserver keyserver.ubuntu.com 29FF164C
echo "deb https://cpkg.datto.com/repositories $(lsb_release -sc) main" | sudo tee /etc/apt/sources.list.d/datto-linux-agent.list
sudo apt-get update
sudo apt-get install dattobd-dkms dattobd-utils
```
### RHEL/CentOS
```bash
# datto-el-rpm-release works on EL6+
curl -O https://cpkg.datto.com/datto-rpm/EnterpriseLinux/7/x86_64/datto-el-rpm-release-1.0.0-1.noarch.rpm
sudo yum localinstall datto-el-rpm-release-1.0.0-1.noarch.rpm
sudo yum install dkms-dattobd dattobd-utils
```
### Fedora
```bash
# datto-fedora-rpm-release works on F20+, excluding rawhide
curl -O https://cpkg.datto.com/datto-rpm/Fedora/22/x86_64/datto-fedora-rpm-release-1.0.0-1.noarch.rpm
sudo yum install datto-fedora-rpm-release-1.0.0-1.noarch.rpm
sudo yum install kernel-devel-$(uname -r) dkms-dattobd dattobd-utils
```

These packages will install and configure the kernel module to start during the boot process. No further configuration should be required when installing using this method.


## From Source

### Dependencies

Note that this build process, while it _should_ work with any distribution, has only been tested with the distributions below.

#### Debian/Ubuntu
```
sudo apt-get install linux-headers-$(uname -r) build-essential
```

#### RHEL/CentOS/Fedora
```
sudo yum install -y kernel-devel-$(uname -r) kernel-headers-$(uname -r) 
sudo yum groupinstall -y "Development Tools"
```

#### OpenSuSE/SLES
```
sudo zypper install kernel-default-devel
sudo zypper install -t pattern devel_C_C++
```

### Getting, Building, and Installing
To retrieve the sources from our Git repository, clone the Git repository to your local computer.

`cd` into the directory created by git, and then run the following commands:
```bash
sudo make
sudo make install
```

### Configuring the Kernel Module
To start the kernel module immediately, run:
```bash
sudo insmod src/dattobd.ko
```

If you would like to have the module be loaded automatically during boot, consult the documentation for your distribution.

### Troubleshooting
On some systems, it may be necessary to let the system know of the location of the shared libraries. If you are having trouble getting `dbdctl` to run, run these two commands:
```
echo /usr/local/{lib,lib64} | sed 's/ /\n/g' | sudo tee /etc/ld.so.conf.d/dattobd.conf
sudo ldconfig
```

### Usage
The kernel module is primarily controlled through `dbdctl(8)`, which was installed previously. For usage instructions, see [dbdctl.8.md](doc/dbdctl.8.md).

