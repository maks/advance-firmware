<!--
SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only

Copyright (c) 2026 xiphonics, inc.

Material inherited from picoTracker v2.3.0 retains its BSD-3-Clause license.
Subsequent original contributions by xiphonics, inc. are licensed under
GPL-3.0-only unless otherwise indicated. Previously granted BSD permissions
and separate third-party licenses remain applicable to their respective portions.

See ../LICENSING.md for the baseline and applicable license notices.
-->

# Picotracker Manual
## Development

While developing and writing content for the manual, you can have a preview of the site by running:
```
./preview.sh
```

If you need to download to run picosite you download a release from GitHub, eg. for a Linux binary:

```
mkdir usermanual/bin; curl -L "https://github.com/maks/picosite/releases/download/0.3.0/picosite-linux" -o "usermanual/bin/picosite"; chmod +x usermanual/bin/picosite
```

When run in preview mode, `picosite` will serve the website on a url of: [http://localhost:8080](http://localhost:8080) and will watch the site directory and its subdirectories for file changes and rebuild the output when it detects modifications to the files.

## Building Outputs

Uses picosite to generate HTML and PDF outputs from the markdown source files.


### HTML

To build the html output run:
```
cd usermanual
./build.sh
```

### PDF

`picosite` can generate a PDF when you pass the `-d` flag with the PDF config file. 

To generate the pico edition PDF run:
```
cd usermanual
./bin/picosite -a static -s pico-edition/content -d pdfconfig.yaml
mv output.pdf picoTracker-pico-user-manual.pdf
```

The generated HTML pages are written to `usermanual/output/` and the PDF is written to `usermanual/output.pdf`, so rename or move the PDF after each build if you want to keep both editions.

## CI

CI will build a PR branch and deploy a preview of it with the url included afterwards as a message on the PR.

## Acknowledgements

Thanks to @squiggythings [WaveTracker documentation website](https://github.com/squiggythings/wavetracker-site) for initial CSS design and example of how to use nice, clean semantic HTML.
