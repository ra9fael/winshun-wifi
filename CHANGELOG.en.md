# Changelog

[中文](CHANGELOG.md)

## [Unreleased]


## [0.2.1] - 2026-11-09

- Mouse-wheel zoom scales the image and window together within the monitor work area.
- Keep focused QR previews open after focus changes; resize with window borders and drag anywhere in the content to move.
- Close the focused preview with Esc, Ctrl+D or Ctrl+W. Right-click to copy the original image, save PNG, restore the default size or close.
- Keep QR modules crisp while resizing, retain the current image snapshot for menu actions, and prevent five-minute idle shutdown while a preview is open.
- Build, test and publish only on new version tags or manual runs; verification defaults to no publication.

## [0.2.0] - 2026-11-09

- Add copy, preview and PNG-save QR actions to saved networks in wifi, with no QR-plugin dependency.
- Read credentials on demand through the existing elevated context, using the actual profile name. Handle hidden networks, escaping, WEP, open and WPA personal networks; refuse enterprise networks and unreadable keys.
- Native previews take foreground and keyboard focus immediately and close with Esc. PNG saves never overwrite files; copy/preview do not persist credentials.
- Add ui and Pictures write permissions; preserve existing connect/disconnect/password shortcuts.

## [0.1.2] - 2026-11-09

### Fixed

- Embed the executable icon and include the ICO and resource script in the package.

### Notes

- Local tag and package only. See docs/icon-validation.en.md for the measured coverage; no GitHub release. Restart the plugin process after upgrading.
