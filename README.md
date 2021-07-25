RestrictEvents
==============

[![Build Status](https://github.com/acidanthera/RestrictEvents/workflows/CI/badge.svg?branch=master)](https://github.com/acidanthera/RestrictEvents/actions) [![Scan Status](https://scan.coverity.com/projects/22252/badge.svg?flat=1)](https://scan.coverity.com/projects/22252)

[Lilu](https://github.com/acidanthera/Lilu) Kernel extension for blocking unwanted processes causing compatibility issues on different hardware and unlocking the support for certain features restricted to other hardware. The list of blocks currently includes:

- `/System/Library/CoreServices/ExpansionSlotNotification`
- `/System/Library/CoreServices/MemorySlotNotification`

The list of patches currently includes:

- Disabled `MacBookAir` model memory replacement UI (comes in pair with `SystemMemoryStatus` = `Upgradable` quirk).
- Disabled `MacPro7,1` PCI Expansion view and RAM view.
- CPU brand string patch for non-Intel CPUs (can be forced for Intel with `revcpu=1`)

_Note_: Apple CPU identifier must be `0x0F01` for 8 core CPUs or higher and `0x0601` for 1, 2, 4, or 6 cores. This is the default in OpenCore for non-natively supported CPUs.

#### Boot arguments
- `-revoff` (or `-liluoff`) to disable
- `-revdbg` (or `-liludbgall`) to enable verbose logging (in DEBUG builds)
- `-revbeta` (or `-lilubetaall`) to enable on macOS older than 10.8 or newer than 12
- `-revproc` to enable verbose process logging (in DEBUG builds)
- `revnopatch=value` to disable patching for userspace processes of Memory/PCI UI and/or CPU renaming or no patching will happen. Accepted values are `all`, `mempci`, `cpuname`, `none` Defaults to `none`.
- `revcpu=value` to enable (`1`, non-Intel default)/disable (`0`, Intel default) CPU brand string patching.
- `revcpuname=value` custom CPU brand string (max 48 characters, 20 or less recommended, taken from CPUID otherwise)

_Note_: `4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:revnopatch`, `4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:revcpu` and `4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:revcpuname` NVRAM variables work the same as the boot arguments, but have lower priority.

#### Credits
- [Apple](https://www.apple.com) for macOS  
- [vit9696](https://github.com/vit9696) for [Lilu.kext](https://github.com/vit9696/Lilu) and great help in implementing some features 
