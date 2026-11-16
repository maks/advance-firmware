# miniz

Vendored from the upstream miniz 3.1.2 release:
https://github.com/richgel999/miniz/releases/tag/3.1.2

Only the reader and inflate APIs are built. The upstream I/O buffer constant
was reduced from 64 KiB to 16 KiB to match the firmware extraction limits.
See `LICENSE` for the upstream license notice.
