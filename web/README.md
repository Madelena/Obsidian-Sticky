# Web installer

This folder is the GitHub Pages site published at
<https://madelena.github.io/Obsidian-Sticky/>. It is a single static page that
loads ESP Web Tools and flashes the Sticky over USB from Chrome or Edge. The
binaries and `manifest.json` it points at are not kept here: the
`.github/workflows/release.yml` build job writes them into `dist/` and the
pages job copies them next to `index.html` before deploying, so opening this
file straight from a checkout will show the page but fail to install.
