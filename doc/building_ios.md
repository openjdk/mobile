# Building the JDK targeting iOS

> [!NOTE]
> Follow the instructions in [building.md](building.md) to make sure you can build the JDK
> targeting a desktop platform before trying to build for iOS

## Build static image for iOS

### Pre-requisites
Following are the prerequisites to build JDK on Mac targeting iOS:
1. Download and unzip pre-built JDK24 to be used as boot JDK
2. Download the [support zip](https://download2.gluonhq.com/mobile/mobile-support-20250106.zip) which contains an ios build for libffi and cups. Unzip it in the root directory of the project.
3. Install `autoconf` on mac via homebrew: `brew install autoconf`

### Clone the mobile repository
Clone the openjdk/mobile repository. The following command can be used:

```
git clone git@github.com:openjdk/mobile.git
```

### Configure
Modify the configure script below so that it has the correct paths to your JDK24 directory,
the unzipped 'support' directory, and the correct location of the iPhoneOS.platform.

```
sh configure \
--with-conf-name=zero-ios-aarch64 \
--disable-warnings-as-errors \
--openjdk-target=aarch64-macos-ios \
--with-boot-jdk=path-to-jdk24 \
--with-jvm-variants=zero \
--with-libffi-include=support/libffi/include \
--with-libffi-lib=support/libffi/libs \
--with-sysroot=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk \
--with-cups-include=support/cups-2.3.6
```

### Build static image
Move into  `build/zero-ios-aarch64` and execute the `make` command:

```
cd build/zero-ios-aarch64
make static-libs-image
```

Once the build is successful, it should have created a directory in `build/zero-ios-aarch64/images/static-libs/lib/zero` with a file `libjvm.a`.