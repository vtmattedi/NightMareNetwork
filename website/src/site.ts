// Site-wide constants. One place for the public URL, the repository and the
// library version, so the head tags, install snippet and MCP command agree.

import library from "../../library.json" with { type: "json" };

export const SITE_URL = "https://nightmare.mattediworks.com";
export const MCP_URL = `${SITE_URL}/mcp`;
export const GITHUB = "https://github.com/vtmattedi/NightMareNetwork";
export const SITE_NAME = "NightMare Network";

/** From library.json, which the PlatformIO registry reads too. */
export const VERSION: string = library.version;
/** The git tag the install snippet pins. Must exist for the snippet to work. */
export const RELEASE_TAG = `v${VERSION}`;
export const LIB_DEPS = `lib_deps =\n    ${GITHUB}.git#${RELEASE_TAG}`;

/** Absolute URL for a site path; page paths end in "/" because every page is a directory. */
export function absolute(path: string): string {
  return SITE_URL + path;
}
