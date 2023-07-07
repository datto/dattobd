# debbuild doesn't define _usrsrc yet
%if "%{_vendor}" == "debbuild"
%global _usrsrc %{_prefix}/src
%endif

# Location to install kernel module sources
%global _kmod_src_root %{_usrsrc}/%{name}-%{version}

# Location for systemd shutdown script
%global _systemd_shutdown /lib/systemd/system-shutdown

# All sane distributions use dracut now, so here are dracut paths for it
%if 0%{?rhel} > 0 && 0%{?rhel} < 7
%global _dracut_modules_root %{_datadir}/dracut/modules.d
%else
%global _dracut_modules_root %{_prefix}/lib/dracut/modules.d
%endif

# RHEL 5 and openSUSE 13.1 and older use mkinitrd instead of dracut
%if 0%{?rhel} == 5 || 0%{?suse_version} > 0 && 0%{?suse_version} < 1315
%global _mkinitrd_scripts_root /lib/mkinitrd/scripts
%endif

# Debian and Ubuntu use initramfs-tools instead of dracut.. for now
%if 0%{?debian} || 0%{?ubuntu}
%global _initramfs_tools_root %{_datadir}/initramfs-tools
%endif

# SUSE hasn't yet reassigned _sharedstatedir to /var/lib, so we force it
# Likewise, RHEL/CentOS 5 doesn't have this fixed either, so we force it there too.
# Debian/Ubuntu doesn't set it there, so we force it for them too
%if 0%{?suse_version} || 0%{?rhel} == 5 || 0%{?debian} || 0%{?ubuntu}
%global _sharedstatedir %{_var}/lib
%endif

# On Debian/Ubuntu systems, /bin/sh usually points to /bin/dash,
# and we need it to be /bin/bash, so we set it here.
%if "%{_vendor}" == "debbuild"
%global _buildshell /bin/bash
%endif

# Set up the correct DKMS module name, per Debian convention for Debian/Ubuntu,
# and use the other name, per convention on all other distros.
%if "%{_vendor}" == "debbuild"
%global dkmsname %{name}-dkms
%else
%global dkmsname dkms-%{name}
%endif

# SUSE Linux does not define the dist tag,
# so we must define it manually
%if "%{_vendor}" == "suse"
%global dist .suse%{?suse_version}

# If SLE 11, redefine it to use SLE prefix
%if 0%{?suse_version} == 1110
%global dist .sle11
%endif

# Return the appropriate tags for SLE 12 and openSUSE 42.1
%if 0%{?suse_version} == 1315
%if 0%{?is_opensuse}
%global dist .suse4200
%else
%global dist .sle12
%endif
%endif

# Return the appropriate tags for SLE 15 and openSUSE 15.0
%if 0%{?suse_version} == 1500
%if 0%{?is_opensuse}
%global dist .suse1500
%else
%global dist .sle15
%endif
%endif

%endif

%if "%{_vendor}" != "debbuild"
%global rpm_dkms_opt 1
%endif

# Set the libdir correctly for Debian/Ubuntu systems
%if "%{_vendor}" == "debbuild"
%global _libdir %{_prefix}/lib/%(%{__dpkg_architecture} -qDEB_HOST_MULTIARCH)
%endif

# Set up library package names properly
%global libprefix libdattobd
%global libsover 1

%if "%{_vendor}" == "debbuild"
%global devsuffix dev
%else
%global devsuffix devel
%endif

%if 0%{?fedora} || 0%{?rhel}
%global libname %{libprefix}
%else
%global libname %{libprefix}%{libsover}
%endif

%global devname %{libprefix}-%{devsuffix}

# For local build stuff, disabled by default
%bcond_with devmode


Name:            dattobd
Version:         0.11.3
Release:         1%{?dist}
Summary:         Kernel module and utilities for enabling low-level live backups
Vendor:          Datto, Inc.
%if "%{_vendor}" == "debbuild"
Packager:        Datto Software Packagers <swpackages@datto.com>
Group:           kernel
License:         GPL-2.0
%else
%if 0%{?suse_version}
Group:           System/Kernel
License:         GPL-2.0
%else
Group:           System Environment/Kernel
License:         GPLv2
%endif
%endif

URL:             https://github.com/datto/dattobd
%if ! %{with devmode}
Source0:         %{url}/archive/v%{version}/%{name}-%{version}.tar.gz
%else
Source0:         %{name}.tar.gz
%endif

BuildRequires:   gcc
BuildRequires:   make
BuildRequires:   rsync

# Some targets (like EL5) expect a buildroot definition
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
The Datto Block Driver is a kernel module that enables
live image backups through block devices.

%package -n %{libname}
Summary:         Library for communicating with %{name} kernel module
%if "%{_vendor}" == "debbuild"
Group:           libs
License:         LGPL-2.1+
%else
%if 0%{?suse_version}
Group:           System/Libraries
License:         LGPL-2.1+
%else
Group:           System Environment/Libraries
License:         LGPLv2+
%endif
%endif

%description -n %{libname}
The library for communicating with the %{name} kernel module.


%package -n %{devname}
Summary:         Files for developing applications to use %{name}.
%if "%{_vendor}" == "debbuild"
Group:           libdevel
License:         LGPL-2.1+
%else
%if 0%{?suse_version}
Group:           Development/Libraries/C and C++
License:         LGPL-2.1+
%else
Group:           Development/Libraries
License:         LGPLv2+
%endif
%endif
Requires:        %{libname}%{?_isa} = %{version}-%{release}

%description -n %{devname}
This package provides the files necessary to develop applications
to use %{name}.


%package utils
Summary:         Utilities for using %{name} kernel module
%if "%{_vendor}" == "debbuild"
Group:           admin
%else
%if 0%{?suse_version}
Group:           System/Kernel
%else
Group:           System Environment/Kernel
%endif
%endif
Requires:        %{dkmsname} = %{version}-%{release}
Requires:        %{libname}%{?_isa} = %{version}-%{release}
%if 0%{?fedora} > 21 || 0%{?rhel} >= 8 || 0%{?suse_version} > 1320 || 0%{?debian} || 0%{?ubuntu}
Recommends:      bash-completion
%endif

%description utils
Utilities for %{name} to use the kernel module.


%package -n %{dkmsname}
Summary:         Kernel module source for %{name} managed by DKMS
%if "%{_vendor}" == "debbuild"
Group:           kernel
%else
%if 0%{?suse_version}
Group:           Development/Sources
%else
Group:           System Environment/Kernel
%endif
%endif

%if 0%{?rhel} != 5 && 0%{?suse_version} != 1110
# noarch subpackages aren't supported in EL5 and SLE11
BuildArch:       noarch
%endif

%if 0%{?rhel} >= 6 || 0%{?fedora} >= 23 || 0%{?suse_version} >= 1315
Requires(preun): dkms >= 2.3
Requires:        dkms >= 2.3
Requires(post):  dkms >= 2.3
%else
Requires(preun): dkms
Requires:        dkms
Requires(post):  dkms
%endif

%if 0%{?rhel}
# Ensure perl is actually installed for builds to work
# This issue mainly affects EL6, but it doesn't hurt to
# be cautious and just require it across the EL releases.
Requires:        perl
%endif

# Dependencies for actually building the kmod
Requires:        make

%if "%{_vendor}" != "debbuild"
%if 0%{?rhel} >= 6 || 0%{?suse_version} >= 1210 || 0%{?fedora}
# With RPM 4.9.0 and newer, it's possible to give transaction
# hints to ensure some kind of ordering for transactions using
# the OrderWithRequires statement.
# More info: http://rpm.org/wiki/Releases/4.9.0#package-building

# This was also backported to RHEL/CentOS 6' RPM, see RH#760793.
# Link: https://bugzilla.redhat.com/760793

# We can use this to ensure that if kernel-devel/kernel-syms is
# in the same transaction as the DKMS module upgrade, it will be
# installed first before processing the kernel module, ensuring
# that DKMS will be able to successfully build against the new
# kernel being installed.
%if 0%{?rhel} || 0%{?fedora}
OrderWithRequires: kernel-devel
%endif

%if 0%{?suse_version}
OrderWithRequires: kernel-syms
%endif

%endif
%endif

%description -n %{dkmsname}
Kernel module sources for %{name} for DKMS to
automatically build and install for each kernel.

%prep
%if ! %{with devmode}
%setup -q
%else
%setup -q -n %{name}
%endif

%build
export CFLAGS="%{optflags}"
make application
make utils

%install
# Install library
mkdir -p %{buildroot}%{_libdir}/pkgconfig
install -p -m 0755 lib/libdattobd.so.%{libsover} %{buildroot}%{_libdir}/
ln -sf libdattobd.so.%{libsover} %{buildroot}%{_libdir}/libdattobd.so
install -p -m 0644 dist/libdattobd.pc.in %{buildroot}%{_libdir}/pkgconfig/libdattobd.pc
mkdir -p %{buildroot}%{_includedir}/dattobd
install -p -m 0644 lib/libdattobd.h %{buildroot}%{_includedir}/dattobd/libdattobd.h
install -p -m 0644 src/dattobd.h %{buildroot}%{_includedir}/dattobd/dattobd.h

sed -e "s:@prefix@:%{_prefix}:g" \
    -e "s:@libdir@:%{_libdir}:g" \
    -e "s:@includedir@:%{_includedir}/dattobd:g" \
    -e "s:@PACKAGE_VERSION@:%{version}:g" \
    -i %{buildroot}%{_libdir}/pkgconfig/libdattobd.pc


# Generate symbols for library package (Debian/Ubuntu only)
%if "%{_vendor}" == "debbuild"
mkdir -p %{buildroot}/%{libname}/DEBIAN
dpkg-gensymbols -P%{buildroot} -p%{libname} -v%{version}-%{release} -e%{buildroot}%{_libdir}/%{libprefix}.so.%{?!libsover:0}%{?libsover} -e%{buildroot}%{_libdir}/%{libprefix}.so.%{?!libsover:0}%{?libsover}.* -O%{buildroot}/%{libname}/DEBIAN/symbols
%endif

# Install utilities and man pages
mkdir -p %{buildroot}%{_bindir}
install -p -m 0755 app/dbdctl %{buildroot}%{_bindir}/dbdctl
mkdir -p %{buildroot}%{_sysconfdir}/bash_completion.d
install -p -m 0755 app/bash_completion.d/dbdctl %{buildroot}%{_sysconfdir}/bash_completion.d/
mkdir -p %{buildroot}%{_mandir}/man8
install -p -m 0644 doc/dbdctl.8 %{buildroot}%{_mandir}/man8/dbdctl.8
install -p -m 0755 utils/update-img %{buildroot}%{_bindir}/update-img
install -p -m 0644 doc/update-img.8 %{buildroot}%{_mandir}/man8/update-img.8

# Install kmod sources
mkdir -p %{buildroot}%{_kmod_src_root}
rsync -av src/ %{buildroot}%{_kmod_src_root}

# Install DKMS configuration
install -m 0644 dist/dattobd-dkms-conf %{buildroot}%{_kmod_src_root}/dkms.conf
sed -i "s/@MODULE_VERSION@/%{version}/g" %{buildroot}%{_kmod_src_root}/dkms.conf

# Install modern modprobe stuff
mkdir -p %{buildroot}%{_sysconfdir}/modules-load.d
install -m 0644 dist/dattobd-modprobe-conf %{buildroot}%{_sysconfdir}/modules-load.d/%{name}.conf

# Legacy automatic module loader for RHEL 5/6
%if 0%{?rhel} && 0%{?rhel} < 7
mkdir -p %{buildroot}%{_sysconfdir}/sysconfig/modules
install -m 0755 dist/dattobd-sysconfig-modules %{buildroot}%{_sysconfdir}/sysconfig/modules/dattobd.modules
%endif

# We only need the hook with older distros
%if 0%{?rhel} == 5 || (0%{?suse_version} && 0%{?suse_version} < 1315) || (0%{?fedora} && 0%{?fedora} < 23)
# Install the kernel hook to enforce dattobd rebuilds
mkdir -p %{buildroot}%{_sysconfdir}/kernel/postinst.d
install -m 755 dist/kernel.postinst.d/50-dattobd %{buildroot}%{_sysconfdir}/kernel/postinst.d/50-dattobd
%endif

# RHEL/CentOS 5 will not have the initramfs scripts because its mkinitrd doesn't support scripts
%if 0%{?rhel} != 5

# Install initramfs stuff
mkdir -p %{buildroot}%{_sharedstatedir}/datto/dla
install -m 755 dist/initramfs/reload %{buildroot}%{_sharedstatedir}/datto/dla/reload

# Debian/Ubuntu use initramfs-tools
%if 0%{?debian} || 0%{?ubuntu}
mkdir -p %{buildroot}%{_initramfs_tools_root}
mkdir -p %{buildroot}%{_initramfs_tools_root}/hooks
mkdir -p %{buildroot}%{_initramfs_tools_root}/scripts/init-premount
install -m 755 dist/initramfs/initramfs-tools/hooks/dattobd %{buildroot}%{_initramfs_tools_root}/hooks/dattobd
install -m 755 dist/initramfs/initramfs-tools/scripts/dattobd %{buildroot}%{_initramfs_tools_root}/scripts/init-premount/dattobd
%else
# openSUSE 13.1 and older use mkinitrd
%if 0%{?suse_version} > 0 && 0%{?suse_version} < 1315
mkdir -p %{buildroot}%{_mkinitrd_scripts_root}
install -m 755 dist/initramfs/initrd/boot-dattobd.sh %{buildroot}%{_mkinitrd_scripts_root}/boot-dattobd.sh
install -m 755 dist/initramfs/initrd/setup-dattobd.sh %{buildroot}%{_mkinitrd_scripts_root}/setup-dattobd.sh
%else
mkdir -p %{buildroot}%{_dracut_modules_root}/90dattobd
install -m 755 dist/initramfs/dracut/dattobd.sh %{buildroot}%{_dracut_modules_root}/90dattobd/dattobd.sh
install -m 755 dist/initramfs/dracut/module-setup.sh %{buildroot}%{_dracut_modules_root}/90dattobd/module-setup.sh
install -m 755 dist/initramfs/dracut/install %{buildroot}%{_dracut_modules_root}/90dattobd/install
%endif
%endif
%endif

# Install systemd shutdown script
mkdir -p %{buildroot}%{_systemd_shutdown}
install -m 755 dist/shutdown/umount_rootfs.shutdown %{buildroot}%{_systemd_shutdown}/umount_rootfs.shutdown

# Get rid of git artifacts
find %{buildroot} -name "*.git*" -print0 | xargs -0 rm -rfv
echo "artifacts are here"
ls
%preun -n %{dkmsname}

%if "%{_vendor}" == "debbuild"
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
%else
if [ $1 -eq 0 ]; then
%endif
    lsmod | grep %{name} >& /dev/null
    if [ $? -eq 0 ]; then
        modprobe -r %{name} || :
    fi
fi

dkms status -m %{name} -v %{version} | grep %{name} >& /dev/null
if [ $? -eq 0 ]; then
    dkms remove -m %{name} -v %{version} --all %{?rpm_dkms_opt:--rpm_safe_upgrade}
fi

%post -n %{dkmsname}

rmmod %{name} &> /dev/null || :

%if "%{_vendor}" == "debbuild"
if [ "$1" = "configure" ]; then
%else
if [ "$1" -ge "1" ]; then
%endif
    if [ -f /usr/lib/dkms/common.postinst ]; then
        /usr/lib/dkms/common.postinst %{name} %{version}
        exit $?
    fi
fi

%post utils
%if 0%{?debian} || 0%{?ubuntu}
%{_initramfs_tools_root}/setup-module-exit-after-umount.sh %{_initramfs_tools_root}/
%else
# openSUSE 13.1 and older use mkinitrd
%if 0%{?suse_version} > 0 && 0%{?suse_version} < 1315
%{_mkinitrd_scripts_root}/setup-module-exit-after-umount.sh %{_mkinitrd_scripts_root}/
%else
#%{_dracut_modules_root}/90dattobd/setup-module-exit-after-umount.sh %{_dracut_modules_root}/90dattobd/
%endif
%endif
%if 0%{?rhel} != 5
# Generate initramfs
if type "dracut" &> /dev/null; then
    echo "Configuring dracut, please wait..."
    dracut -f || :
elif type "mkinitrd" &> /dev/null; then
    echo "Configuring initrd, please wait..."
    mkinitrd || :
elif type "update-initramfs" &> /dev/null; then
    echo "Configuring initramfs, please wait..."
    update-initramfs -u || :
fi
sleep 1 || :
%endif

%postun utils
%if 0%{?rhel} != 5
%if 0%{?debian} || 0%{?ubuntu}
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
%else
if [ $1 -eq 0 ]; then
%endif

        if type "dracut" &> /dev/null; then
                echo "Configuring dracut, please wait..."
                dracut -f || :
        elif type "mkinitrd" &> /dev/null; then
                echo "Configuring initrd, please wait..."
                mkinitrd || :
        elif type "update-initramfs" &> /dev/null; then
                echo "Configuring initramfs, please wait..."
                update-initramfs -u || :
        fi
fi
%endif
%if 0%{?debian} || 0%{?ubuntu}
%{_initramfs_tools_root}/restore-default-umount.sh
%else
# openSUSE 13.1 and older use mkinitrd
%if 0%{?suse_version} > 0 && 0%{?suse_version} < 1315
%{_mkinitrd_scripts_root}/restore-default-umount.sh
%else
%{_dracut_modules_root}/90dattobd/restore-default-umount.sh
%endif
%endif

%post -n %{libname}
/sbin/ldconfig

%postun -n %{libname}
/sbin/ldconfig

%clean
# EL5 and SLE 11 require this section
rm -rf %{buildroot}

%files utils
%if 0%{?suse_version}
%defattr(-,root,root,-)
%endif
%{_bindir}/dbdctl
%{_bindir}/update-img
%{_sysconfdir}/bash_completion.d/dbdctl
%{_mandir}/man8/dbdctl.8*
%{_mandir}/man8/update-img.8*
# Initramfs scripts for all but RHEL 5
%if 0%{?rhel} != 5
%dir %{_sharedstatedir}/datto/dla
%{_sharedstatedir}/datto/dla/reload
%if 0%{?debian} || 0%{?ubuntu}
%{_initramfs_tools_root}/hooks/dattobd
%{_initramfs_tools_root}/scripts/init-premount/dattobd
%{_initramfs_tools_root}/setup-module-exit-after-umount.sh
%{_initramfs_tools_root}/call-rmmod-after-umount.sh
%{_initramfs_tools_root}/restore-default-umount.sh
%else
%if 0%{?suse_version} > 0 && 0%{?suse_version} < 1315
%{_mkinitrd_scripts_root}/boot-dattobd.sh
%{_mkinitrd_scripts_root}/setup-dattobd.sh
%{_mkinitrd_scripts_root}/setup-module-exit-after-umount.sh
%{_mkinitrd_scripts_root}/call-rmmod-after-umount.sh
%{_mkinitrd_scripts_root}/restore-default-umount.sh
%else
%dir %{_dracut_modules_root}/90dattobd
%{_dracut_modules_root}/90dattobd/setup-module-exit-after-umount.sh
%{_dracut_modules_root}/90dattobd/call-rmmod-after-umount.sh
%{_dracut_modules_root}/90dattobd/restore-default-umount.sh
%{_dracut_modules_root}/90dattobd/dattobd.sh
%{_dracut_modules_root}/90dattobd/module-setup.sh
%{_dracut_modules_root}/90dattobd/install
%endif
%endif
%endif

# Install systemd shutdown script
%{_systemd_shutdown}/umount_rootfs.shutdown

%doc README.md doc/STRUCTURE.md
%if "%{_vendor}" == "redhat"
%{!?_licensedir:%global license %doc}
%license COPYING* LICENSING.md
%else
%if "%{_vendor}" == "debbuild"
%license dist/copyright
%else
%doc COPYING* LICENSING.md
%endif
%endif

%files -n %{libname}
%if 0%{?suse_version}
%defattr(-,root,root,-)
%endif
%{_libdir}/libdattobd.so.%{libsover}
%if "%{_vendor}" == "redhat"
%{!?_licensedir:%global license %doc}
%license COPYING* LICENSING.md
%else
%if "%{_vendor}" == "debbuild"
%license dist/copyright
%else
%doc COPYING* LICENSING.md
%endif
%endif

%files -n %{devname}
%if 0%{?suse_version}
%defattr(-,root,root,-)
%endif
%{_libdir}/libdattobd.so
%{_libdir}/pkgconfig/libdattobd.pc
%{_includedir}/dattobd/
%if "%{_vendor}" == "redhat"
%{!?_licensedir:%global license %doc}
%license COPYING* LICENSING.md
%else
%if "%{_vendor}" == "debbuild"
%license dist/copyright
%else
%doc COPYING* LICENSING.md
%endif
%endif

%files -n %{dkmsname}
%if 0%{?suse_version}
%defattr(-,root,root,-)
%endif
%if 0%{?rhel} == 5 && 0%{?rhel} == 6 && 0%{?suse_version} == 1110
# RHEL/CentOS 5/6 and SLE 11 don't support this at all
%exclude %{_sysconfdir}/modules-load.d/dattobd.conf
%else
%config %{_sysconfdir}/modules-load.d/dattobd.conf
%endif
%if 0%{?rhel} && 0%{?rhel} < 7
%config %{_sysconfdir}/sysconfig/modules/dattobd.modules
%endif
%dir %{_kmod_src_root}
%{_kmod_src_root}/*.c
%{_kmod_src_root}/*.h
%{_kmod_src_root}/configure-tests
%{_kmod_src_root}/dkms.conf
%{_kmod_src_root}/genconfig.sh
%{_kmod_src_root}/Makefile
%if 0%{?rhel} == 5 || (0%{?suse_version} && 0%{?suse_version} < 1315) || (0%{?fedora} && 0%{?fedora} < 23)
%dir %{_sysconfdir}/kernel/postinst.d
%{_sysconfdir}/kernel/postinst.d/50-dattobd
%endif
%doc README.md
%if "%{_vendor}" == "redhat"
%{!?_licensedir:%global license %doc}
%license COPYING* LICENSING.md
%else
%if "%{_vendor}" == "debbuild"
%license dist/copyright
%else
%doc COPYING* LICENSING.md
%endif
%endif

%changelog
* Tue May 19 2023 Lukasz Fulek <lukasz.fulek@datto.com> - 0.11.3
- Fix memory leak on Ubuntu 20.04

* Tue Feb 7 2023 Dakota Williams <drwilliams@datto.com> - 0.11.2
- Similar update to configure test

* Tue Jan 31 2023 Dakota Williams <drwilliams@datto.com> - 0.11.1
- Fix for bad configure test

* Wed Jan 25 2023 Dakota Williams <drwilliams@datto.com> - 0.11.0
- Add support for kernels up to 5.15

* Fri Nov 20 2020 Dakota Williams <drwilliams@datto.com> - 0.10.16
- Revert "Revert "Fix for userspace pointer leak""
- Revert "Revert "Fix for deb9 SCSI drives""
- Revert "Revert "Additional fix for multipage bios in kernel 5.4""
- Corruption fix found on CentOS 6/7

* Wed Nov 18 2020 Neal Gompa <ngompa@datto.com> - 0.10.15
- Revert "Fix for userspace pointer leak"
- Revert "Fix for deb9 SCSI drives"
- Revert "Additional fix for multipage bios in kernel 5.4"

* Fri Oct 16 2020 Dakota Williams <drwilliams@datto.com> - 0.10.14
- Fix for error message during kernel module installation
- Fix for userspace pointer leak
- Fix for deb9 SCSI drives
- Fix for bare words comparison in rpm spec
- Additional fix for multipage bios in kernel 5.4

* Wed Feb 5 2020 Dakota Williams <drwilliams@datto.com> - 0.10.13
- Fix for -Wframe-larger-than

* Fri Jan 24 2020 Dakota Williams <drwilliams@datto.com> - 0.10.12
- Build feature tests out-of-tree and parallelize execution of them
- Explicitly add incidental build deps to spec file
- Added missing mount/umount userspace pointer conversion for logs
- Removed references to removed kernel build variable SUBDIRS, required for kernel 5.4

* Mon Sep 23 2019 Dakota Williams <drwilliams@datto.com> - 0.10.11
- Use upstream names for sector size and CR0 register page protect bit
- Changed symbol tests to use /lib/modules system maps and then fallback to /boot if it doesn't exist
- Added Linux 5.1+ compatibility

* Wed Aug 21 2019 Dakota Williams <drwilliams@datto.com> - 0.10.10
- Added Linux 5.0 compatibility
- Changed location of custom bio flag
- Fixed to use the head of compound pages instead of tail
- Added control to get free minor

* Fri Jan 11 2019 Tim Crawford <tcrawford@datto.com> - 0.10.9
- Fixed procfs output with small reads
- Fixed mnt_want_write feature test

* Thu Dec 6 2018 Tim Crawford <tcrawford@datto.com> - 0.10.8
- Fixed procfs output on Linux 4.19
- Fixed writing out the header to the COW file
- Fixed GPF caused by accessing poisoned mappings in pages
- Fixed building Debian packages with debbuild 18.9

* Mon Sep 17 2018 Tim Crawford <tcrawford@datto.com> - 0.10.7
- Added field to track the number of changed blocks
- Changed the initramfs scripts to make the root device read-only during mount
- Changed number of supported concurrent devices to be between 1 and 255
- Changed behavior on kernel upgrade to not remove the module if DKMS is >= 2.3

* Wed Jul 18 2018 Tim Crawford <tcrawford@datto.com> - 0.10.6
- Added targets for making RPMs and DEBs to the Makefile
- Moved the initramfs hook earlier in the boot so reload scripts are run before the root file system is mounted
- Fixed unmount check that prevented persist-through-reboot from working correctly
- Fixed crash when handling a REQ_OP_WRITE_ZEROES request

* Tue Jun 26 2018 Tim Crawford <tcrawford@datto.com> - 0.10.5
- Add support for openSUSE Leap 15

* Wed Feb 14 2018 Tim Crawford <tcrawford@datto.com> - 0.10.4
- Added Linux 4.14 compatibility
- Added support for running the feature tests against non-running kernel
- Added an uninstall target for make
- Fixed NULL pointer dereference when reading a page

* Wed Nov 15 2017 Neal Gompa <ngompa@datto.com> - 0.10.3
- Fix Makefile to have no trailing slash for BASE_DIR to fix debug symbols

* Wed Nov 15 2017 Neal Gompa <ngompa@datto.com> - 0.10.2
- Correctly pass compile flags to fix the build for Fedora 27

* Tue Nov 7 2017 Tim Crawford <tcrawford@datto.com> - 0.10.1
- Switched to DKMS common scriplet for installing the module
- Suppressed warnings for failed memory allocations if fallback is used
- Added Linux 4.13 compatibility
- Improved detection of root block device for dracut systems

* Tue Sep 5 2017 Tim Crawford <tcrawford@datto.com> - 0.10.0
- Fixed module loading on RHEL 5/6
- Made use of `OrderWithRequires` to improve module upgrades
- Fixed bug in info ioctl
- Added man pages to utils package
- Prefixed all library functions with `dattobd`
- Made module parameters lower case
- Added packages for shared library
- Made utilities use the shared library by default
- Added compatibility with RHEL 7.4

* Mon Mar 6 2017 Tim Crawford <tcrawford@datto.com> - 0.9.16
- Added Linux 4.10 compatibility
- Removed unused feature tests
- Added tests for the kernel module
- Changed debug logging from a compile-time option to a run-time option

* Tue Feb 14 2017 Tim Crawford <tcrawford@datto.com> - 0.9.15
- Fixed race condition during transition to incremental
- Made MAY_HOOK_SYSCALLS and MAX_SNAP_DEVICES parameters readable in sysfs
- Moved initramfs scripts from the DKMS package to the utils package

* Tue Jan 24 2017 Tim Crawford <tcrawford@datto.com> - 0.9.14
- Fixed REQ_DISCARD usage
- Fixed initramfs rebuild process

* Tue Jan 17 2017 Tim Crawford <tcrawford@datto.com> - 0.9.13
- Fixed snapshot performance issue introduced in 0.9.11
- Released packaging and distribution-specific files

* Mon Dec 19 2016 Neal Gompa <ngompa@datto.com> - 0.9.12
- Updated to 0.9.12
- Added file for auto-loading module on RHEL/CentOS 5/6

* Thu Nov 10 2016 Neal Gompa <ngompa@datto.com> - 0.9.11
- Updated to 0.9.11

* Wed Oct 12 2016 Neal Gompa <ngompa@datto.com> - 0.9.10
- Updated to 0.9.10
- Dropped constraint on kernel >= 2.6.32

* Wed Sep  7 2016 Neal Gompa <ngompa@datto.com> - 0.9.9
- Updated to 0.9.9
- Disabled stripping symbols from dattobd.ko

* Wed Jul 20 2016 Neal Gompa <ngompa@datto.com> - 0.9.8
- Updated to 0.9.8

* Thu Jul  7 2016 Neal Gompa <ngompa@datto.com> - 0.9.7
- Updated to 0.9.7

* Fri Apr 15 2016 Neal Gompa <ngompa@datto.com> - 0.9.6
- Updated to 0.9.6

* Wed Mar 30 2016 Neal Gompa <ngompa@datto.com> - 0.9.5
- Updated to 0.9.5

* Fri Feb 26 2016 Neal Gompa <ngompa@datto.com> - 0.9.0
- Updated to 0.9.0
- img-merge changed to update-img

* Wed Feb 24 2016 Neal Gompa <ngompa@datto.com> - 0.8.15
- Updated to 0.8.15

* Wed Feb 17 2016 Neal Gompa <ngompa@datto.com> - 0.8.14
- Updated to 0.8.14

* Fri Jan 22 2016 Neal Gompa <ngompa@datto.com> - 0.8.13
- Updated to 0.8.13

* Thu Dec 17 2015 Neal Gompa <ngompa@datto.com> - 0.8.12
- Updated to 0.8.12

* Wed Oct 28 2015 Neal Gompa <ngompa@datto.com> - 0.8.11-1
- Updated to 0.8.11

* Tue Sep  8 2015 Neal Gompa <ngompa@datto.com> - 0.8.10-1
- Updated to 0.8.10

* Mon Aug 17 2015 Neal Gompa <ngompa@datto.com> - 0.8.9-1
- Updated to 0.8.9

* Mon Aug 10 2015 Neal Gompa <ngompa@datto.com> - 0.8.8-1
- Updated URL to point to public GitHub repo
- Added license data
- Updated to 0.8.8 [Jul 21]

* Mon Jul 20 2015 Neal Gompa <ngompa@datto.com> - 0.8.7-1
- Updated to 0.8.7

* Fri Jul 17 2015 Neal Gompa <ngompa@datto.com> - 0.8.6-1
- Updated to 0.8.6
- Added bash completion script for dbdctl

* Sat Jun 13 2015 Neal Gompa <ngompa@datto.com> - 0.8.5-3
- Add conditional to prevent module removal on upgrade
- Update URL field

* Thu Jun 11 2015 Neal Gompa <ngompa@datto.com> - 0.8.5-2
- Made dkms commands compatible with older versions
- Added test loop to try to ensure module is started
- Updated licensing to match public release

* Wed Jun 10 2015 Neal Gompa <ngompa@datto.com> - 0.8.5-1
- Update to 0.8.5

* Tue Jun  9 2015 Neal Gompa <ngompa@datto.com> - 0.8.4-1
- Update to 0.8.4
- Remove unnecessary kmod_{name,version} globals

* Fri Jun  5 2015 Neal Gompa <ngompa@datto.com> - 0.8.3-2
- Remove unnecessary evaluation step to ID older kmods

* Thu Jun  4 2015 Neal Gompa <ngompa@datto.com> - 0.8.3-1
- Updated to 0.8.3
- Removed ExclusiveArch
- Added noarch BuildArch to dkms subpackage
- Removed dracut/initramfs stuff as they are unnecessary

* Fri May 29 2015 Neal Gompa <ngompa@datto.com> - 0.8.2-1
- Initial packaging of dattobd kmod and utils
