# Changelog

This changelog starts at `1.2.0.24`, the point where CPR-vCodex began tracking release changes in this file.

| Version | Changes |
|---|---|
| `1.2.0.27-x3` | - Added official X3 support. The firmware now auto-detects X3 vs X4 at runtime via I2C probe, no separate build required.<br>- X3 grayscale rendering uses dedicated LUTs from upstream [#1607](https://github.com/crosspoint-reader/crosspoint-reader/pull/1607): tuned VDL drive strengths, tight scan timing, and fast-diff BB reinforcement to reduce ghosting and white stroke artifacts.<br>- Submodule updated to `a64a3c2` (open-x4-epaper/community-sdk [#32](https://github.com/open-x4-epaper/community-sdk/pull/32)) which carries the X3 gray LUT implementation.<br>- `DirectPixelWriter` and `ScreenshotUtil` now use runtime display dimensions, supporting X3's 792×528 resolution alongside X4's 800×480.<br>- Added `env:default_x3` and `env:gh_release_x3` PlatformIO environments producing X3-labelled artifacts.<br>- Removed the "incidental X3 compatibility" disclaimer from README. |
| `1.2.0.26` | - Hardened book completion tracking so achievements still register when leaving from explicit `End of book` states.<br>- Applied the completion fix consistently across EPUB, TXT, and XTC readers. |
| `1.2.0.25` | - Added PNG sleep image compatibility while keeping BMP sleep images unchanged.<br>- PNG sleep images now work as transparent overlays on top of the last visible screen.<br>- Sleep preview now recognizes and previews both BMP and PNG files. |
| `1.2.0.24` | - Reviewed and corrected all 23 bundled UI languages.<br>- Synced the fork with upstream through `64f5ef0`, including keyboard, XTC, parser, docs, font, and i18n updates.<br>- Fixed runtime UI font replacement so Vietnamese glyphs render correctly.<br>- Normalized translation newlines and multiline text handling again after the YAML/i18n regressions. |
