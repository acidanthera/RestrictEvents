RestrictEvents
==============

[![Build Status](https://github.com/acidanthera/RestrictEvents/workflows/CI/badge.svg?branch=master)](https://github.com/acidanthera/RestrictEvents/actions) [![Scan Status](https://scan.coverity.com/projects/22252/badge.svg?flat=1)](https://scan.coverity.com/projects/22252)

[Lilu](https://github.com/acidanthera/Lilu) Kernel extension for blocking unwanted processes causing compatibility issues on different hardware and unlocking the support for certain features restricted to other hardware. The list of blocks currently includes:

- `/System/Library/CoreServices/ExpansionSlotNotification`
- `/System/Library/CoreServices/MemorySlotNotification`

The list of patches currently includes:

- Disabled `MacBookAir` model memory replacement UI (comes in pair with `SystemMemoryStatus` = `Upgradable` quirk).
- Disabled `MacPro7,1` PCI Expansion view and RAM view.
- CPU brand string patch for non-Intel CPUs (can be forced for Intel with `revcpu=1`).
- Disabled uninitialized disk UI

_Note_: Apple CPU identifier must be `0x0F01` for 8 core CPUs or higher and `0x0601` for 1, 2, 4, or 6 cores. This is the default in OpenCore for non-natively supported CPUs.

#### Boot arguments
- `-revoff` (or `-liluoff`) to disable
- `-revdbg` (or `-liludbgall`) to enable verbose logging (in DEBUG builds)
- `-revbeta` (or `-lilubetaall`) to enable on macOS older than 10.8 or newer than 14
- `-revproc` to enable verbose process logging (in DEBUG builds)
- `revpatch=value` to enable patching as comma separated options. Default value is `auto`.
  - `memtab` - enable memory tab in System Information on MacBookAir and MacBookPro10,x platforms
  - `pci` - prevent PCI configuration warnings in System Settings on MacPro7,1 platforms
  - `cpuname` - custom CPU name in System Information
  - `diskread` - disables uninitialized disk warning in Finder
  - `asset` - allows Content Caching when `sysctl kern.hv_vmm_present` returns `1` on macOS 11.3 or newer
  - `sbvmm` - forces VMM SB model, allowing OTA updates for unsupported models on macOS 11.3 or newer
  - `f16c` - resolve CoreGraphics crashing on Ivy Bridge CPUs by disabling f16c instruction set reporting in macOS 13.3 or newer
  - `none` - disable all patching
  - `auto` - same as `memtab,pci,cpuname`, without `memtab` and `pci` patches being applied on real Macs
- `revcpu=value` to enable (`1`, non-Intel default)/disable (`0`, Intel default) CPU brand string patching.
- `revcpuname=value` custom CPU brand string (max 48 characters, 20 or less recommended, taken from CPUID otherwise)
- `revblock=value` to block processes as comma separated options. Default value is `auto`.
  - `pci` - prevent PCI and RAM configuration notifications on MacPro7,1 platforms
  - `gmux` - block displaypolicyd on Big Sur+ (for genuine MacBookPro9,1/10,1)
  - `media` - block mediaanalysisd on Ventura+ (for Metal 1 GPUs)
  - `none` - disable all blocking
  - `auto` - same as `pci`

_Note_: `4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:revpatch`, `4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:revcpu`, `4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:revcpuname` and `4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:revblock` NVRAM variables work the same as the boot arguments, but have lower priority.

#### Removing badges

If using RestrictEvents to block PCI and RAM configuration notifications, they will go away, but the alert in the Apple menu will stay. To get rid of this alert, run the following commands:

```
defaults delete com.apple.SlotNotificationsPref memoryBadgeCount
defaults delete com.apple.SlotNotificationsPref expansionBadgeCount
```

<!-- For posterity
- Software update badge:
  - Monterey and lower: `defaults delete com.apple.systempreferences AttentionPrefBundleIDs` (checks for `com.apple.preferences.softwareupdate`)
  - Ventura and higher: `defaults delete ~/Library/Preferences/com.apple.systempreferences.plist AttentionPrefBundleIDs` (checks for `com.apple.FollowUpSettings.FollowUpSettingsExtension`)
    - Full path is required because `defaults` attempts to access a sandboxed path otherwise
- App store updates: `defaults delete com.apple.appstored BadgeCount`
-->

#### Credits
- [Apple](https://www.apple.com) for macOS
- [vit9696](https://github.com/vit9696) for [Lilu.kext](https://github.com/vit9696/Lilu) and great help in implementing some features
