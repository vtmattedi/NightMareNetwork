// Build-time git facts: the revision being built, when each document last
// changed, and the latest commits. Everything degrades to "unknown"/empty when
// the build has no .git (an exported tree, a Docker context without it), so a
// missing history never fails the build; it only hides those details.

import { execFileSync } from "node:child_process";

function git(repo: string, args: string[]): string | undefined {
  try {
    return execFileSync("git", ["-C", repo, ...args], { encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] }).trim();
  } catch {
    return undefined;
  }
}

export interface Change {
  sha: string;
  date: string;
  subject: string;
}

export interface GitInfo {
  /** Short hash of HEAD, or undefined outside a git checkout. */
  revision?: string;
  /** Commit date of HEAD, ISO 8601. */
  date?: string;
  /** Last commit date per repository path ("docs/mcp.md"), ISO 8601. Empty in a shallow clone. */
  updated: Record<string, string>;
  /** Latest non-merge commits touching the library or its documentation, newest first. */
  changes: Change[];
}

export function readGit(repo: string): GitInfo {
  const revision = git(repo, ["rev-parse", "--short", "HEAD"]);
  if (!revision) return { updated: {}, changes: [] };
  const date = git(repo, ["show", "-s", "--format=%cI", "HEAD"]);

  // Per-file dates need the whole history: in a shallow clone every file
  // older than the cut-off would claim the cut-off commit's date.
  const updated: Record<string, string> = {};
  if (git(repo, ["rev-parse", "--is-shallow-repository"]) === "false") {
    const log = git(repo, ["log", "--format=%x00%cI", "--name-only", "--", "docs"]) ?? "";
    for (const entry of log.split("\0").slice(1)) {
      const [when, ...files] = entry.trim().split("\n");
      for (const f of files) if (f && !(f in updated)) updated[f] = when;
    }
  }

  const changes = (git(repo, ["log", "-n", "15", "--no-merges", "--format=%h%x09%cI%x09%s", "--", "src", "docs", "library.json"]) ?? "")
    .split("\n")
    .filter(Boolean)
    .map((line) => {
      const [sha, when, ...subject] = line.split("\t");
      return { sha, date: when, subject: subject.join("\t") };
    });

  return { revision, date, updated, changes };
}
