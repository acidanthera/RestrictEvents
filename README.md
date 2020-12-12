RestrictEvents
==============

[![Build Status](https://github.com/acidanthera/RestrictEvents/workflows/CI/badge.svg?branch=master)](https://github.com/acidanthera/RestrictEvents/actions)

Kernel extension for blocking unwanted processes causing compatibility issues on different hardware. Currently includes:

- `/System/Library/CoreServices/MemorySlotNotification`
- `/usr/libexec/dp2hdmiupdater`
- `/usr/libexec/efiupdater`
- `/usr/libexec/firmwarecheckers/eficheck/eficheck`
- `/usr/libexec/sdfwupdater`
- `/usr/libexec/smcupdater`
- `/usr/libexec/ssdupdater`
- `/usr/libexec/usbcupdater`
- `/usr/libexec/vbiosupdater`

#### Boot arguments
- `-revoff` (or `-liluoff`) to disable
- `-revdbg` (or `-liludbgall`) to enable verbose logging (in DEBUG builds)
- `-revbeta` (or `-lilubetaall`) to enable on macOS older than 10.8 or newer than 11
- `-revproc` to enable verbose process logging (in DEBUG builds)

#### Credits
- [Apple](https://www.apple.com) for macOS  
- [vit9696](https://github.com/vit9696) for [Lilu.kext](https://github.com/vit9696/Lilu) and great help in implementing some features 
