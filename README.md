RestrictEvents
==============

[![Build Status](https://github.com/acidanthera/RestrictEvents/workflows/CI/badge.svg?branch=master)](https://github.com/acidanthera/RestrictEvents/actions) [![Scan Status](https://scan.coverity.com/projects/22252/badge.svg?flat=1)](https://scan.coverity.com/projects/22252)

[Lilu](https://github.com/acidanthera/Lilu) Kernel extension for blocking unwanted processes causing compatibility issues on different hardware and unlocking the support for certain features restricted to other hardware. The list of blocks currently includes:

- `/System/Library/CoreServices/ExpansionSlotNotification`
- `/System/Library/CoreServices/MemorySlotNotification`
- `/usr/libexec/firmwarecheckers/eficheck/eficheck`

The list of patches currently includes:

- Disabled `MacBookAir` model memory replacement UI (comes in pair with `SystemMemoryStatus` = `Upgradable` quirk).
- Disabled `MacPro7,1` PCI Expansion view and RAM view.

#### Boot arguments
- `-revoff` (or `-liluoff`) to disable
- `-revdbg` (or `-liludbgall`) to enable verbose logging (in DEBUG builds)
- `-revbeta` (or `-lilubetaall`) to enable on macOS older than 10.8 or newer than 11
- `-revproc` to enable verbose process logging (in DEBUG builds)
- `-revnopatch` to disable patching for userspace processes

#### Credits
- [Apple](https://www.apple.com) for macOS  
- [vit9696](https://github.com/vit9696) for [Lilu.kext](https://github.com/vit9696/Lilu) and great help in implementing some features 
