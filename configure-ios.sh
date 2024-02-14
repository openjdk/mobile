# sample configure command for ios

bash configure \
--enable-headless-only \
--with-boot-jdk=/opt/jdk-22.jdk/Contents/Home \
--with-build-jdk=/opt/jdk-23.jdk/Contents/Home \
--openjdk-target=aarch64-macos-ios \
--with-cups-include=../support/cups-2.3.6 \
--with-sysroot=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk

