# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- jxlazy: allow querying extra channel blend info.
- jxlazy: new `unbounded` indicator to identify boxes that extend to the end of the file.
- `add` and `subtract` command line modes.
- `--optimize` option for `merge` mode, currently only supporting automatic frame crop.
- `compare` command line mode.
- `alphaFill` JSON key.

### Changed

- Logic for deciding default data type used for samples has changed to be slightly
  more cautious if "unusual" blending is used.  (It defaults to float more often.)
- Implicit alpha channels added during a merge operation are initialised to subjectively
  more useful values when `alphaFill` isn't specified.

### Fixed

- Merge mode: setting the color profile from an external icc doesn't work.
- jxlazy: incorrect size check when decompressing boxes causes an error.

## [0.0.1] - 2026-01-19

### Added

- jxltk

[unreleased]: https://github.com/alistair7/jxltk/compare/v0.0.1...HEAD
[0.0.1]: https://github.com/alistair7/jxltk/releases/tag/v0.0.1
