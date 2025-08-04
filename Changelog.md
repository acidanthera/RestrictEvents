RestrictEvents Changelog
========================
#### v1.1.5
- Fixed loading on macOS 10.10 and older due to a MacKernelSDK regression

#### v1.1.4
- Added constants for macOS 15 support

#### v1.1.3
- Expanded `sbvmm` to support more environments:
  - macOS install applications (ex. `Install macOS Sonoma.app`)
  - Booted installer and recoveryOS
    - `-no_compat_check` or boot.efi patch required to pass bootloaders' compatibility checks

#### v1.1.2
- Added constants for macOS 14 support

#### v1.1.1
- Fixed `f16c` patch misreporting other `hw.optional` features

#### v1.1.0
- Added `hw.optional.f16c` disabling for macOS 13.3+
  - Resolves CoreGraphics.framework invoking AVX2.0 code paths on Ivy Bridge CPUs
  - Configurable via `revpatch`'s `f16c` argument

#### v1.0.9
- Added `revblock` for user configuration of blocking processes
- Added additional process blocking:
  - `gmux` - block displaypolicyd on Big Sur+ (for genuine MacBookPro9,1/10,1)
  - `media` - block mediaanalysisd on Ventura+ (for Metal 1 GPUs)
  - `pci` - block PCIe & memory notifications (for MacPro7,1 SMBIOS)
    - Previous unconditional
  - `auto` - same as `pci`, set by default

#### v1.0.8
- Added constants for macOS 13 support
- Do not enable Memory and PCI UI patching on real Macs in `auto` mode
- Added `MacPro7,1` memory patch for macOS 13 `System Settings`->`General`->`About`

#### v1.0.7
- Fixed detecting CPU core count on Intel CPU with altered topology
- Fixed support on macOS 10.9 - 10.11
- Improve configuration of enabled patches

#### v1.0.6
- Fixed memory view restrictions for `MacBookAir` and `MacBookPro10` not being correctly disabled
- Disabled `The disk you inserted was not readable by this computer` message popup
- Added Content Caching support for systems exposing `kern.hv_vmm_present` via `-revasset`
- Lowered OS requirement for `-revsbvmm` to macOS 11.3

#### v1.0.5
- Added macOS 12 software update support with any Mac model via `-revsbvmm`

#### v1.0.4
- Fixed dual-core CPU spoofing on macOS 10.14 and earlier
- Allow preserving MP7,1 UI through `revnopatch` in NVRAM or boot-args
- Skip leading spaces for automatically received CPU names

#### v1.0.3
- Added constants for macOS 12 support
- Rewrote `eficheck` restrictions to avoid slowdowns

#### v1.0.2
- Fixed patching CPU brand string with 8 core configurations
- Fixed detecting CPU core count on some CPU models
- Added single-core CPU brand string spoofing support

#### v1.0.1
- Disabled `MacPro7,1` RAM & PCI Expansion Slot UIs
- Disabled `MacBookAir` memory view restrictions
- Added CPU brand string patch

#### v1.0.0
- Initial release
