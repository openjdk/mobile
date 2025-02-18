# Building the JDK targeting iOS

Following are the instructions for building JDK on mac targeting iOS.

## Build static image for mac (Test)
The following steps are to test if you can build the static image targeting mac.
If you do not want to do this, feel free to jump directly to [Build static image for iOS](#build-static-image-for-iOS).

### Pre-requisites
Download and unzip pre-built JDK24 to be used as boot JDK.

### Clone the mobile repository
Clone the openjdk/mobile repository. The following command can be used:

```
git clone git@github.com:openjdk/mobile.git
```

### Configure and build static image
Use the following commands configure and create an image:

```
sh configure --with-boot-jdk=path-to-jdk24
cd build/macosx-aarch64-server-release
make static-libs-image
```

Once the build is successful, it should have created a directory in `build/macosx-aarch64-server-release/images/static-libs/lib/server` with a file `libjvm.a`.

## Build static image for iOS

### Pre-requisites
Following are the prerequisites to build JDK on Mac targeting iOS:
1. Download and unzip pre-built JDK24 to be used as boot JDK
2. Download the support zip which contains an ios build for libffi and cups. Unzip it in the root directory of the project.  // TODO: add link to support zip
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