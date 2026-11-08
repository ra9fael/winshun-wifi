# Changelog

[中文](CHANGELOG.md)

## [Unreleased]


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
