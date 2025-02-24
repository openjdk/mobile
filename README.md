# Welcome to the JDK!

For build instructions please see the
[online documentation](https://openjdk.org/groups/build/doc/building.html),
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
--with-boot-jdk=/Users/abhinay/.sdkman/candidates/java/24.ea.29-open \
--with-jvm-variants=zero \
--with-libffi-include=<support-dir-absolute-path>/libffi/include \
--with-libffi-lib=<support-dir-absolute-path>/libffi/libs \
--with-cups-include=<support-dir-absolute-path>/cups-2.3.6
--with-sysroot=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk \
```

### Build static image
Execute the `make` command:

```
make CONF=zero-ios-aarch64 static-libs-image
```

Once the build is successful, it should have created a directory in `build/zero-ios-aarch64/images/static-libs/lib/zero` with a file `libjvm.a`.