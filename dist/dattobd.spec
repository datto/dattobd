# debbuild doesn't define _usrsrc yet
%if %{_vendor} == "debbuild"
%global _usrsrc %{_prefix}/src
%endif

%define _kmod_src_root %{_usrsrc}/%{name}-%{version}

# On Debian/Ubuntu systems, /bin/sh usually points to /bin/dash,
# and we need it to be /bin/bash, so we set it here.
%if %{_vendor} == "debbuild"
%global _buildshell /bin/bash
%global ___build_args %{?nil}
%endif

# Set up the correct DKMS module name, per Debian convention for Debian/Ubuntu,
# and use the other name, per convention on all other distros.
%if %{_vendor} == "debbuild"
%define dkmsname %{name}-dkms
%else
%define dkmsname dkms-%{name}
%endif

# SUSE Linux does not define the dist tag,
# so we must define it manually
%if %{_vendor} == "suse"
%define dist .suse%{?suse_version}

# If SLE 11, redefine it to use SLE prefix
%if 0%{?suse_version} == 1110
%define dist .sle11
%endif

# Return the appropriate tags for SLE 12 and openSUSE 42.1
%if 0%{?suse_version} == 1315
%if 0%{?is_opensuse}
%define dist .suse4200
%else
%define dist .sle12
%endif
%endif

%endif

%if %{_vendor} != "debbuild"
%global rpm_dkms_opt 1
%endif


Name:            dattobd
Version:         0.9.12
Release:         1%{?dist}
Summary:         Kernel module and utilities for enabling low-level live backups
Vendor:          Datto, Inc.
%if %{_vendor} == "debbuild"
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
Source0:         https://github.com/datto/dattobd/archive/v%{version}/%{name}-%{version}.tar.gz
Source1:         generic-dkms-conf
Source2:         generic-modprobe-conf

BuildRequires:   rsync
# Some targets (like EL5) expect a buildroot definition
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
The Datto Block Driver is a kernel module that enables
live image backups through block devices.


%package utils
Summary:         Utilities for using %{name} kernel module
%if %{_vendor} == "debbuild"
Group:           admin
%else
%if 0%{?suse_version}
Group:           System/Kernel
%else
Group:           System Environment/Kernel
%endif
%endif
Requires:        %{dkmsname} = %{version}-%{release}
%if 0%{?fedora} > 21 || 0%{?rhel} >= 8 || 0%{?suse_version} > 1320 || 0%{?debian} || 0%{?ubuntu}
Recommends:      bash-completion
%endif


%description utils
Utilities for %{name} to use the kernel module.

%package -n %{dkmsname}
Summary:         Kernel module source for %{name} managed by DKMS
%if %{_vendor} == "debbuild"
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

Requires(preun): dkms
Requires:        dkms
Requires(post):  dkms

%if 0%{?rhel}
# Ensure perl is actually installed for builds to work
# This issue mainly affects EL6, but it doesn't hurt to
# be cautious and just require it across the EL releases.
Requires:        perl
%endif


%description -n %{dkmsname}
Kernel module sources for %{name} for DKMS to
automatically build and install for each kernel.

%prep
%setup -q

%build
make application
make utils


%install
mkdir -p %{buildroot}%{_bindir}
install -p -m 0755 app/dbdctl %{buildroot}%{_bindir}/dbdctl
mkdir -p %{buildroot}%{_sysconfdir}/bash_completion.d
mv app/bash_completion.d/dbdctl %{buildroot}%{_sysconfdir}/bash_completion.d/
install -p -m 0755 utils/update-img %{buildroot}%{_bindir}/update-img
mkdir -p %{buildroot}%{_kmod_src_root}
rsync -av src/ %{buildroot}%{_kmod_src_root}
mkdir -p %{buildroot}%{_sysconfdir}/modules-load.d
install -m 0644 %{SOURCE1} %{buildroot}%{_kmod_src_root}/dkms.conf
sed -i "s/@MODULE_NAME@/%{name}/g" %{buildroot}%{_kmod_src_root}/dkms.conf
sed -i "s/@MODULE_VERSION@/%{version}/g" %{buildroot}%{_kmod_src_root}/dkms.conf
install -m 0644 %{SOURCE2} %{buildroot}%{_sysconfdir}/modules-load.d/%{name}.conf
sed -i "s/@MODULE_NAME@/%{name}/g" %{buildroot}%{_sysconfdir}/modules-load.d/%{name}.conf

%if 0%{?rhel} && 0%{?rhel} < 7
mkdir -p %{buildroot}%{_sysconfdir}/sysconfig/modules
cat > %{buildroot}%{_sysconfdir}/sysconfig/modules/dattobd.modules <<EOF_SYSCONFIGMODULES
#!/bin/sh

if [ ! -c /dev/datto-ctl ] ; then
  exec /sbin/modprobe dattobd >/dev/null 2>&1
fi
EOF_SYSCONFIGMODULES
chmod +x %{buildroot}%{_sysconfdir}/sysconfig/modules/dattobd.modules
%endif

# Get rid of git artifacts
find %{buildroot} -name "*.git*" -print0 | xargs -0 rm -rfv

%preun -n %{dkmsname}

%if %{_vendor} == "debbuild"
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
%else
if [ $1 -eq 0 ]; then
%endif
  lsmod | grep %{name} >& /dev/null
  if [ $? -eq 0 ]; then
    modprobe -r %{name}
  fi
fi

dkms status -m %{name} -v %{version} | grep %{name} >& /dev/null
if [ $? -eq 0 ]; then
  dkms remove -m %{name} -v %{version} --all %{?rpm_dkms_opt:--rpm_safe_upgrade}
fi

%post -n %{dkmsname}

rmmod %{name} &> /dev/null

dkms add -m %{name} -v %{version} %{?rpm_dkms_opt:--rpm_safe_upgrade}
dkms build -m %{name} -v %{version}
dkms install -m %{name} -v %{version}

sleep 5s

echo "Starting kernel module..."
modprobe %{name}
sleep 1s

if [[ $(lsmod | grep -o %{name}) != *"%{name}"* ]]; then
    echo "Module unable to start!"
    echo "Please start it with 'modprobe %{name}' after install."
else
    echo "Module started!"
fi

%clean
rm -rf %{buildroot}

%files utils
%if 0%{?suse_version}
%defattr(-,root,root,-)
%endif
%{!?_licensedir:%global license %doc}
%{_bindir}/dbdctl
%{_bindir}/update-img
%{_sysconfdir}/bash_completion.d/dbdctl
%doc README.md
%if %{_vendor} == "redhat"
%license COPYING
%else
%doc COPYING
%endif


%files -n %{dkmsname}
%if 0%{?suse_version}
%defattr(-,root,root,-)
%endif
%{!?_licensedir:%global license %doc}
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
%{_kmod_src_root}/Makefile
%{_kmod_src_root}/configure-tests
%{_kmod_src_root}/dattobd.c
%{_kmod_src_root}/dattobd.h
%{_kmod_src_root}/dkms.conf
%{_kmod_src_root}/genconfig.sh
%{_kmod_src_root}/includes.h
%doc README.md
%if %{_vendor} == "redhat"
%license COPYING
%else
%doc COPYING
%endif


%changelog
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

