# Cryonero
Cryonero: secure, private, untraceable and fast cryptocurrency

## About

Welcome to the repository of cryonero Core. Here you will find source code, instructions, wiki resources, and integration tutorials.

Contents
* Building on Linux 64-bit
* Building on Mac OSX
* Building on Windows
* Building on other platforms

## Building on Linux 64-bit

All commands below are adapted for Ubuntu, other distributions may need an other command set.

### Building with standard options

Create directory `cryonerodev` somewhere and go there:
```
$> mkdir cryonerodev
$> cd cryonerodev
```

To go futher you have to have a number of packages and utilities. You need at least gcc 5.4.

* `build-essential` package:
    ```
    $> sudo apt-get install build-essential
    ```

* CMake (3.5 or newer):
    ```
    $> sudo apt-get install cmake
    $> cmake --version
    ```
    If version is too old, follow instructions on [the official site](https://cmake.org/download/).

* Boost (1.64 or newer):
    You need boost in `cryonerodev` folder. We do not configure to use boost installed by `apt-get`, because it is sometimes updated without your control by installing some unrelated packages. Also some users reported crashes after `find_package` finds headers from one version of boost and libraries from different version, or if installed boost uses dynamic linking.
    ```
    $cryonerodev> wget -c 'https://dl.bintray.com/boostorg/release/1.65.1/source/boost_1_65_1.tar.gz'
    $cryonerodev> tar xf boost_1_65_1.tar.gz
    $cryonerodev> mv boost_1_65_1 boost
    $cryonerodev> cd boost
    $cryonerodev/boost> ./bootstrap.sh
    $cryonerodev/boost> ./b2 link=static cxxflags="-fPIC" linkflags="-pie" -j 8 --build-dir=build64 --stagedir=stage
    cd ..
    ```

Git-clone (or git-pull) cryonero Core source code in that folder:
```
$cryonerodev> git clone https://github.com/Cryonero-Project/cryonero
```

Put LMDB source code in `cryonerodev` folder (source files are referenced via relative paths, so you do not need to separately build it):
```
$cryonerodev> git clone https://github.com/LMDB/lmdb.git
```

Create build directory inside cryonerocoin, go there and run CMake and Make:
```
$cryonerodev> mkdir cryonero/build
$cryonerodev> cd cryonero/build
$cryonerodev/cryonero/build> cmake ..
$cryonerodev/cryonero/build> time make -j4
```



### Building with specific options

Install OpenSSL to `cryonerodev/openssl` folder. (Use switch `linux-x86_64-clang` instead of `linux-x86_64` if using clang.)
```
$cryonerodev> git clone https://github.com/openssl/openssl.git
$cryonerodev> cd openssl
$cryonerodev/openssl> ./Configure linux-x86_64 no-shared
$cryonerodev/openssl> time make -j4
$cryonerodev/openssl> cd ..
```

Download amalgamated [SQLite 3](https://www.sqlite.org/download.html) and unpack it into `cryonerodev/sqlite` folder (source files are referenced via relative paths, so you do not need to separately build it).

Below are the commands which add OpenSSL support and switch from LMDB to SQLite by providing options to CMake:

```
$cryonerodev> mkdir cryonero/build
$cryonerodev> cd cryonero/build
$cryonerodev/cryonero/build> cmake -DUSE_SSL=1 -DUSE_SQLITE=1 ..
$cryonerodev/cryonero/build> time make -j4
```

## Building on Mac OSX

### Building with standard options (10.11 El Capitan or newer)

You need command-line tools. Either get XCode from an App Store or run 'xcode-select --install' in terminal and follow instructions. First of all, you need [Homebrew](https://brew.sh).

Then open terminal and install CMake and Boost:

* `brew install cmake`
* `brew install boost`

Create directory `cryonerodev` somewhere and go there:
```
$~/Downloads> mkdir <path-to-cryonero-folder>
$~/Downloads> cd <path-to-cryonero-folder>
```

Git-clone (or git-pull) cryonero Core source code in that folder:
```
$cryonerodev> git clone https://github.com/Cryonero-Project/cryonero
```

Put LMDB source code in `cryonerodev` folder (source files are referenced via relative paths, so you do not need to separately build it):
```
$cryonerodev> git clone https://github.com/LMDB/lmdb.git
```

Create build directory inside cryonero_core, go there and run CMake and Make:
```
$cryonerodev> mkdir cryonero/build
$cryonerodev> cd cryonero/build
$cryonerodev/cryonero/build> cmake -DUSE_SSL=0 ..
$cryonerodev/cryonero/build> time make -j4
```

### Building with specific options

Binaries linked with Boost installed by Homebrew will work only on your computer's OS X version or newer, but not on older versions like El Capitan.

If you need binaries to run on all versions of OS X starting from El Capitan, you need to build boost yourself targeting El Capitan SDK.

Download [Mac OSX 10.11 SDK](https://github.com/phracker/MacOSX-SDKs/releases) and unpack to it into `Downloads` folder

Download and unpack [Boost](https://boost.org) to `Downloads` folder.

Then build and install Boost:
```
$~> cd ~/Downloads/boost_1_67_0/
$~/Downloads/boost_1_67_0> ./bootstrap.sh
$~/Downloads/boost_1_67_0> ./b2 -a -j 4 cxxflags="-stdlib=libc++ -std=c++14 -mmacosx-version-min=10.11 -isysroot/Users/user/Downloads/MacOSX10.11.sdk -fPIC" linkflags="-pie" install`
```

Install OpenSSL to `cryonerodev/openssl` folder:
```
$~/Downloads/cryonerodev> git clone https://github.com/openssl/openssl.git
$~/Downloads/cryonerodev> cd openssl
```

If you need binaries to run on all versions of OS X starting from El Capitan, you need to build OpenSSL targeting El Capitan SDK.
```
$cryonerodev/openssl> ./Configure darwin64-x86_64-cc no-shared -mmacosx-version-min=10.11 -isysroot/Users/user/Downloads/MacOSX10.11.sdk
```
Otherwise just use
```
$cryonerodev/openssl> ./Configure darwin64-x86_64-cc no-shared
```

```
$cryonerodev/openssl> time make -j4
$cryonerodev/openssl> cd ..
```

Download amalgamated [SQLite 3](https://www.sqlite.org/download.html) and unpack it into `cryonerodev/sqlite` folder (source files are referenced via relative paths, so you do not need to separately build it).

You add OpenSSL support or switch from LMDB to SQLite by providing options to CMake:

```
$cryonerodev> mkdir cryonero/build
$cryonerodev> cd cryonero/build
$cryonerodev/cryonero/build> cmake -DUSE_SSL=1 -DUSE_SQLITE=1 ..
$cryonerodev/cryonero/build> time make -j4
```

## Building on Windows

You need Microsoft Visual Studio Community 2017. [Download](https://www.visualstudio.com/vs/) and install it selecting `C++`, `git`, `cmake integration` packages.
Run `Visual Studio x64 command prompt` from start menu.

Create directory `cryonerodev` somewhere:
```
$C:\> mkdir cryonerodev
$C:\> cd cryonerodev
```

Get [Boost](https://boost.org) and unpack it into a folder inside `cryonerodev` and rename it from `boost_1_66_0` or similar to just `boost`.

Build boost (build 32-bit boost version only if you need 32-bit cryonero Core binaries).
```
$> cd boost
$C:\cryonerodev\boost> bootstrap.bat
$C:\cryonerodev\boost> b2.exe address-model=64 link=static -j 8 --build-dir=build64 --stagedir=stage
$C:\cryonerodev\boost> b2.exe address-model=32 link=static -j 8 --build-dir=build32 --stagedir=stage32
cd ..
```

Git-clone (or git-pull) cryonero Core source code in that folder:
```
$C:\cryonerodev> git clone https://github.com/Cryonero-Project/cryonero
```

Put LMDB in the same folder (source files are referenced via relative paths, so you do not need to separately build it):
```
$C:\cryonerodev> git clone https://github.com/LMDB/lmdb.git
```

You need to build openssl, first install ActivePerl (select "add to PATH" option, then restart console):
```
$C:\cryonerodev> git clone https://github.com/openssl/openssl.git
$C:\cryonerodev> cd openssl
$C:\cryonerodev\openssl> perl Configure VC-WIN64A no-shared no-asm
$C:\cryonerodev\openssl> nmake
$C:\cryonerodev\openssl> cd ..
```
If you want to build 32-bit binaries, you will also need 32-bit build of openssl in separate folder (configuring openssl changes header files, so there is no way to have both 32-bit and 64-bit versions in the same folder):
```
$C:\cryonerodev> git clone https://github.com/openssl/openssl.git openssl32
$C:\cryonerodev> cd openssl32
$C:\cryonerodev\openssl> perl Configure VC-WIN32 no-shared no-asm
$C:\cryonerodev\openssl> nmake
$C:\cryonerodev\openssl> cd ..
```

Now launch Visual Studio, in File menu select `Open Folder`, select `C:\cryonerodev\cryonero` folder.
Wait until CMake finishes running and `Build` appears in main menu.
Select `x64-Debug` or `x64-Release` from standard toolbar, and then `Build/Build Solution` from the main menu.

You cannot add options to CMake running inside Visual Studio so just edit `CMakeLists.txt` and set `USE_SSL` or `USE_SQLITE` to `ON` if you wish to build with them.

## Building on 32-bit x86 platforms, iOS, Android and other ARM platforms

cryonero Core works on 32-bit systems if SQLite is used instead of LMDB (we've experienced lots of problems building and running with lmdb in 32-bit compatibility mode, especially on iOS).

Therefore SQLite option is automatically selected by CMake on 32-bit platforms and you must have SQLite downloaded as explained in appropriate sections above.

We build official x86 32-bit version for Windows only, because there is zero demand for 32-bit version for Linux or Mac.

Building source code for iOS, Android, Raspberry PI, etc is possible (we have experimental `cryonerod` and `wallet-rpc` running on ARM64 iPhone) but requires major skills on your part. __TBD__

## Building on Big-Endian platforms

Currently it is impossible to run cryonero Core on any Big-Endian platform, due to lots of endianess-dependent code. This may be fixed in the future. If you wish to run on Big-Endian platform, please contact us.

## Building with parameters

If you want to use tools like `clang-tidy`, run `cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..` instead of `cmake ..`

