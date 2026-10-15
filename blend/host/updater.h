// Auto updater for the Windows co-op host. See updater.c.
#ifndef PICORI_UPDATER_H
#define PICORI_UPDATER_H

struct UpdaterOptions {
	const char* source; // extra place to look (folder, manifest URL or github:owner/repo), or NULL
	int assumeYes;      // install without asking
	int onlyUpdate;     // run from --update-only: report results on stdout
};

// Looks for a newer build and offers to install it. Returns 1 when an update was
// installed and the new version was started, so the caller should exit.
int updaterRun(const struct UpdaterOptions* opts);

// The version this program was built as ("1.2"), or "dev".
const char* updaterVersion(void);

#endif
