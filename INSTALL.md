# elastio-snap INSTALL

## From Repositories
Elastio Software Inc. provides repositories for x86_64 editions of the RHEL/CentOS starting from the version 6.10, Amazon Linux 2, Fedora 31 and newer, Debian 8 and newer, and Ubuntu LTS starting from the version 16.04.
We recommend that you install the kernel module from Elastio's repositories.

### Repository package installation for RPM-based systems

#### RHEL/CentOS
The repository install package `elastio-repo` is available for CentOS/RHEL 6.10 and newer.

```bash
# Install repo package
sudo yum localinstall https://repo.assur.io/master/linux/rpm/CentOS/$(rpm -E %rhel)/x86_64/Packages/elastio-repo-0.0.2-1.el$(rpm -E %rhel).noarch.rpm
```

#### Amazon Linux
The repository install package `elastio-repo` is available for Amazon Linux 2.

```bash
# Install repo package
sudo yum localinstall https://repo.assur.io/master/linux/rpm/Amazon/$(rpm -E %%amzn)/x86_64/Packages/elastio-repo-0.0.2-1.amzn$(rpm -E %amzn).noarch.rpm
```

#### Fedora
The repository install package `elastio-repo` is available for Fedora 31 and newer.
```bash
# Install repo package
sudo yum install https://repo.assur.io/master/linux/rpm/Fedora/$(rpm -E %fedora)/x86_64/Packages/elastio-repo-0.0.2-1.fc$(rpm -E %fedora).noarch.rpm
```

### Elastio Snap installation

We are ready to install Elastio Snap packages after the repository package has been installed:

```bash
sudo yum install dkms-elastio-snap elastio-snap-utils
```

### Repository package installation for DEB-based systems

#### Debian / Ubuntu LTS
The repository install package `elastio-repo` is available for Debian 8 (jessie) and newer.
The same packages are applicable for Ubuntu LTS starting from 16.04 (xenial) and newer.
```bash
# Install prerequisites. This is not necessary in the most cases except pure docker.
sudo apt-get update
sudo apt-get install wget gnupg

# Detect Debian version
debian_ver=$(grep VERSION_ID /etc/os-release | tr -cd [0-9])

# Ubuntu? 
[ $debian_ver > 1000 ] && debian_ver=$(($debian_ver/200))
# Download repo package and install it
wget https://repo.assur.io/master/linux/deb/Debian/${debian_ver}/pool/elastio-repo_0.0.2-1debian${debian_ver}_all.deb
sudo dpkg -i elastio-repo_0.0.2-1debian${debian_ver}_all.deb
sudo apt-get update
```

### Elastio Snap installation

We are ready to install Elastio Snap packages after the repository package has been installed:

```bash
# Install Elastio Snap
sudo apt-get install elastio-snap-dkms elastio-snap-utils
```

These packages will install and configure the kernel module to start during the boot process. No further configuration should be required when installing using this method.

## From Source

### Dependencies

Note that this build process, while it _should_ work with any distribution, has only been tested with the distributions below. The lowest supported kernel is 2.6.18.

#### Debian/Ubuntu
```
sudo apt-get install linux-headers-$(uname -r) build-essential
```

#### RHEL/CentOS/Fedora
```
sudo yum install kernel-devel-$(uname -r)
sudo yum groupinstall "Development Tools"
```

#### openSUSE/SLE
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
sudo modprobe elastio-snap
```

If you would like to have the module be loaded automatically during boot, consult the documentation for your distribution.

### Troubleshooting
On some systems, it may be necessary to let the system know of the location of the shared libraries. If you are having trouble getting `elioctl` to run, run these two commands:
```
echo /usr/local/{lib,lib64} | sed 's/ /\n/g' | sudo tee /etc/ld.so.conf.d/elastio-snap.conf
sudo ldconfig
```

### Usage
The kernel module is primarily controlled through `elioctl(8)`, which was installed previously. For usage instructions, see [elioctl.8.md](doc/elioctl.8.md).
