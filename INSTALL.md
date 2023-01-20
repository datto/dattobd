# dattobd INSTALL

## From the openSUSE Build Service
We offer [the latest mainline code from the openSUSE Build Service for a number of Linux distributions](https://software.opensuse.org//download.html?project=isv%3ADatto%3ALinuxAgent%3ADattoBD%3AMainline&package=dattobd).

## From Datto Repositories
We recommend that you install the kernel module from Datto's repositories. Datto provides repos for x86-64 editions of RHEL/CentOS, Fedora, openSUSE, SUSE Linux Enterprise, Debian, and Ubuntu LTS.

### RHEL/CentOS
The repository install package `datto-el-rpm-release` is available for EL6+.
```bash
sudo yum localinstall https://cpkg.datto.com/datto-rpm/repoconfig/datto-el-rpm-release-$(rpm -E %rhel)-latest.noarch.rpm
sudo yum install dkms-dattobd dattobd-utils
```
### Fedora
The repository install package `datto-fedora-rpm-release` is available for F26+, excluding Rawhide.
```bash
sudo yum install https://cpkg.datto.com/datto-rpm/repoconfig/datto-fedora-rpm-release-$(rpm -E %fedora)-latest.noarch.rpm
sudo yum install kernel-devel-$(uname -r) dkms-dattobd dattobd-utils
```
### openSUSE
The repository install package `datto-opensuse-rpm-release` is available for openSUSE Leap 15.x.
#### openSUSE Leap 15.x
```bash
sudo zypper install https://cpkg.datto.com/datto-rpm/repoconfig/datto-opensuse-rpm-release-15.0-latest.noarch.rpm
sudo zypper install dkms-dattobd dattobd-utils
```
### SUSE Linux Enterprise
The repository install package `datto-sle-rpm-release` is available for SUSE Linux Enterprise 12 SP3 and higher.
#### SUSE Linux Enterprise 12 SP3
Due to the DKMS software not being present in SLE's default repositories, we provide a usable copy in
our repositories.
```bash
ktype=$(uname -r | awk -F '-' '{ print $NF }')
kver=$(uname -r | sed "s/-${ktype}//")
sudo zypper install https://cpkg.datto.com/datto-rpm/repoconfig/datto-sle-rpm-release-12-latest.noarch.rpm
sudo zypper install -C "kernel-syms = ${kver}"
sudo zypper install dkms-dattobd dattobd-utils
```
#### SUSE Linux Enterprise 15
Due to the DKMS software not being present in SLE's default repositories, [PackageHub](https://packagehub.suse.com/) or other similar
repository providing DKMS is required. The steps below assume PackageHub is not enabled, so it may not be necessary to enable
PackageHub if you can already get DKMS through an already-installed repository.
```bash
ktype=$(uname -r | awk -F '-' '{ print $NF }')
kver=$(uname -r | sed "s/-${ktype}//")
sudo SUSEConnect -p PackageHub/15/x86_64
sudo zypper install https://cpkg.datto.com/datto-rpm/repoconfig/datto-sle-rpm-release-15-latest.noarch.rpm
sudo zypper install -C "kernel-syms = ${kver}"
sudo zypper install dkms-dattobd dattobd-utils
```
### Debian/Ubuntu LTS
```bash
sudo apt-key adv --fetch-keys https://cpkg.datto.com/DATTO-PKGS-GPG-KEY
echo "deb [arch=amd64] https://cpkg.datto.com/datto-deb/public/$(lsb_release -sc) $(lsb_release -sc) main" | sudo tee /etc/apt/sources.list.d/datto-linux-agent.list
sudo apt-get update
sudo apt-get install dattobd-dkms dattobd-utils
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
sudo modprobe dattobd
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
