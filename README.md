# Welcome to the JDK!

For build instructions please see the
[online documentation](https://git.openjdk.org/jdk/blob/master/doc/building.md),
or either of these files:

- [doc/building.html](doc/building.html) (html version)
- [doc/building.md](doc/building.md) (markdown version)

See <https://openjdk.org/> for more information about the OpenJDK
Community and the JDK and see <https://bugs.openjdk.org> for JDK issue
tracking.

## Build static image for iOS

> [!NOTE]
> Follow the instructions in [building.md](building.md) to make sure
> you can build the JDK targeting macOS before trying to build for iOS

### Pre-requisites
Following are the prerequisites to build JDK on Mac targeting iOS:
1. Download and install JDK24 for macOS. You can use the JDK you just built targeting macOS in the above note instead.
2. Download the [support zip](https://download2.gluonhq.com/mobile/mobile-support-20250106.zip) which contains an ios build for libffi and cups. Unzip it in an easy to use location.
3. Install `autoconf` on mac via homebrew: `brew install autoconf`

### Clone the mobile repository
Clone the openjdk/mobile repository. The following command can be used:

```
git clone git@github.com:openjdk/mobile.git
```

### Configure
Modify the configure script below so that it has the correct paths to the unzipped 'support'
directory, and the correct location of the iPhoneOS.platform.

```
sh configure \
--disable-warnings-as-errors \
--openjdk-target=aarch64-macos-ios \
--with-libffi-include=<support-dir-path>/libffi/include \
--with-libffi-lib=<support-dir-path>/libffi/libs \
--with-cups-include=<support-dir-path>/cups-2.3.6
--with-sysroot=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk \
```

If you need to tell configure the path of your boot JDK, or if configure fails with an error saying
it can't find a boot JDK, for instance if you downloaded a JDK in compressed archive form rather
than with an installer, you can pass `--with-boot-jdk=<java-directory-path>` to configure.

For iOS the default JVM used is Zero, since iOS has no writeable and executable sections. However, if
you plan to run the JDK on the simulator for testing purposes, you can use the other JVM variants such
as Server. To do this, pass `--with-jvm-variants=server` to configure (Or any other valid option, which
are, in no particular order: server, client, minimal, core, zero, custom). Do note that passing zero to
this option is redundant since Zero is already the default for iOS.

### Build static image
Execute the `make` command:

```
make CONF=ios-aarch64-zero-release static-libs-image
```

Once the build is successful, it should have created a directory in `build/ios-aarch64-zero-release/images/static-libs/lib/zero` with a file `libjvm.a`.
