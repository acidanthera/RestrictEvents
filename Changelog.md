RestrictEvents Changelog
========================
#### v1.0.4
- Fixed dual-core CPU spoofing on macOS 10.14 and earlier

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
