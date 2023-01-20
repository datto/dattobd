# Building the package

This package is built using [debbuild](https://github.com/debbuild/debbuild) for Debian targets.

The Makefile included in this project helps you to do builds, but you need
to have debbuild installed first.

You can install debbuild from the openSUSE Build Service for either [Debian](https://software.opensuse.org//download.html?project=Debian%3Adebbuild&package=debbuild) or [Ubuntu](https://software.opensuse.org//download.html?project=Ubuntu%3Adebbuild&package=debbuild).

Once installed, `make deb` will let you build a Debian package.

For building RPMs, you just need `rpmbuild`. For example, on Fedora:

```bash
$ sudo dnf install rpm-build

```

Once installed, `make rpm` will let you build an RPM package.
