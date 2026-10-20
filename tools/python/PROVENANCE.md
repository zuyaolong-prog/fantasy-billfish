# Bundled Python

Official CPython Windows x64 embeddable runtime, pinned to `3.14.8`.
Release: https://www.python.org/downloads/release/python-3148/
Archive: https://www.python.org/ftp/python/3.14.8/python-3.14.8-embed-amd64.zip
Official published SHA-256 and independently downloaded archive SHA-256:
`a93abe456ab01bd96d7a085b3cdb6566b3063f4241360d114142fbdb07f0a310`.

Downloaded and verified on 2026-10-18. The extracted `python.exe` has a Valid
Windows Authenticode signature from `Python Software Foundation`, certificate
thumbprint `E875769F82782E3C919984CF747D21B943E0AEBE`.
`runtime-files.json` records the unmodified extracted files and their SHA-256.
`LICENSE.txt` is supplied by the official archive.

The standalone runtime requires no Python installation, pip packages, PATH edits,
administrator rights or user-site packages. The shipped `python314._pth` remains
unchanged: only its standard-library ZIP and runtime directory are on the initial
path. Billfish helpers explicitly add their own installed automation directory.
No registry Python installation or browser credentials are imported.

The root launcher passes this interpreter to its child core through
`BILLFISH_PYTHON`, unless an explicit environment override is already supplied.
Native-host registration uses this runtime by default and generates absolute
paths for the destination computer. The CLI also uses this runtime directly.

Source-only exports retain this document, license, path configuration and file
manifest. To obtain the runtime, download the exact archive above, verify its
published SHA-256 and extract its contents into this directory. Do not copy
another computer's Python installation or user-site package directory.
