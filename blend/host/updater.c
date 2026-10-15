// Auto updater for the Windows co-op host.
//
// On start the game looks for a newer build, picori-seal-vX.Y.zip (the zip the
// build makes, see package.sh), and offers to install it. Builds hold the
// player's own ROM, so they never come from a public server. It looks in:
//   - the player's Downloads folder and an "updates" folder next to the game,
//   - any "source =" in update.ini next to the game: another folder (a synced
//     OneDrive/Dropbox/Google Drive folder works), an http(s) URL of a
//     latest.json manifest, or github:owner/repo for the latest release of a
//     private GitHub repo, read with the "token =" from update.ini.
// Installing unpacks the zip next to the game, keeps the old version and the
// saves in a "previous" folder, and restarts the game.
#include "updater.h"

#ifndef PICORI_VERSION
#define PICORI_VERSION "dev"
#endif

const char* updaterVersion(void) {
	return PICORI_VERSION;
}

#ifndef _WIN32

int updaterRun(const struct UpdaterOptions* opts) {
	(void) opts;
	return 0;
}

#else

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <zlib.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TITLE "Zelda: The Picori Seal"
#define ZIP_PREFIX "picori-seal-v"
#define MAX_SOURCES 8
#define PATH_LEN 1024

struct Version {
	int part[3];
};

struct Candidate {
	struct Version version;
	char versionText[32];
	char location[PATH_LEN]; // local zip path, or URL to download
	char from[PATH_LEN];     // where it was found, for the prompt
	char sha256[65];
	int remote;
	int githubAsset; // download needs the GitHub token
};

struct Config {
	char sources[MAX_SOURCES][PATH_LEN];
	int sourceCount;
	char token[256];
	int check;
	int ask;
	char skip[32];
	char installed[32]; // the last version this updater installed
};

static char exeDir[PATH_LEN];
static char exePath[PATH_LEN];
static int verbose;

static void say(const char* fmt, ...) {
	if (!verbose)
		return;
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	putchar('\n');
	fflush(stdout);
}

// ---- strings and paths (UTF-8 inside, UTF-16 for Windows calls) ----

static wchar_t* widen(const char* s) {
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	wchar_t* w = malloc(n * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
	return w;
}

static void narrow(const wchar_t* w, char* out, size_t size) {
	if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int) size, NULL, NULL))
		out[0] = 0;
}

static void joinPath(char* out, size_t size, const char* dir, const char* name) {
	size_t n = strlen(dir);
	snprintf(out, size, "%s%s%s", dir, (n && (dir[n - 1] == '\\' || dir[n - 1] == '/')) ? "" : "\\", name);
}

static int fileExists(const char* path) {
	wchar_t* w = widen(path);
	DWORD a = GetFileAttributesW(w);
	free(w);
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int moveFile(const char* from, const char* to) {
	wchar_t* wf = widen(from);
	wchar_t* wt = widen(to);
	BOOL ok = MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
	free(wf);
	free(wt);
	return ok;
}

static int copyFile(const char* from, const char* to) {
	wchar_t* wf = widen(from);
	wchar_t* wt = widen(to);
	BOOL ok = CopyFileW(wf, wt, FALSE);
	free(wf);
	free(wt);
	return ok;
}

static void deleteFile(const char* path) {
	wchar_t* w = widen(path);
	DeleteFileW(w);
	free(w);
}

static void makeDir(const char* path) {
	wchar_t* w = widen(path);
	CreateDirectoryW(w, NULL);
	free(w);
}

static FILE* openFile(const char* path, const char* mode) {
	wchar_t* w = widen(path);
	wchar_t* m = widen(mode);
	FILE* f = _wfopen(w, m);
	free(w);
	free(m);
	return f;
}

static void trim(char* s) {
	char* start = s;
	while (*start == ' ' || *start == '\t')
		start++;
	memmove(s, start, strlen(start) + 1);
	size_t n = strlen(s);
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
		s[--n] = 0;
}

// ---- versions ----

// Reads "1.2", "v1.2.3" or "1.2 (1)"; returns 0 when there is no version.
static int parseVersion(const char* s, struct Version* v, char* text, size_t textSize) {
	memset(v, 0, sizeof(*v));
	if (*s == 'v' || *s == 'V')
		s++;
	if (*s < '0' || *s > '9')
		return 0;
	const char* start = s;
	for (int i = 0; i < 3; i++) {
		v->part[i] = (int) strtol(s, (char**) &s, 10);
		if (*s != '.' || s[1] < '0' || s[1] > '9')
			break;
		s++;
	}
	if (text) {
		size_t n = (size_t) (s - start);
		if (n >= textSize)
			n = textSize - 1;
		memcpy(text, start, n);
		text[n] = 0;
	}
	return 1;
}

static int compareVersions(const struct Version* a, const struct Version* b) {
	for (int i = 0; i < 3; i++)
		if (a->part[i] != b->part[i])
			return a->part[i] < b->part[i] ? -1 : 1;
	return 0;
}

// ---- settings ----

static void writeTemplate(const char* path) {
	FILE* f = openFile(path, "w");
	if (!f)
		return;
	fputs("; Zelda: The Picori Seal updater settings.\n"
	      ";\n"
	      "; When the game starts it looks for a newer version (picori-seal-vX.Y.zip) in your\n"
	      "; Downloads folder and in the \"updates\" folder next to the game, and offers to\n"
	      "; install it. Your saves are kept, and the old version goes in the \"previous\" folder.\n"
	      ";\n"
	      "; More places to look, one per line (remove the ; in front):\n"
	      ";   source = D:\\OneDrive\\Picori Seal              a folder, e.g. one synced from the cloud\n"
	      ";   source = https://example.com/latest.json       a latest.json manifest (see package.sh)\n"
	      ";   source = github:yourname/your-private-repo     the latest release of a GitHub repo\n"
	      "; A private GitHub repo also needs a fine-grained token with read-only access to it:\n"
	      ";   token = github_pat_...\n"
	      ";\n"
	      "; check = 0 turns update checks off; ask = 0 installs new versions without asking.\n"
	      "check = 1\n"
	      "ask = 1\n", f);
	fclose(f);
}

static void readConfig(struct Config* c) {
	char path[PATH_LEN], line[PATH_LEN + 32];
	memset(c, 0, sizeof(*c));
	c->check = 1;
	c->ask = 1;
	joinPath(path, sizeof(path), exeDir, "update.ini");
	FILE* f = openFile(path, "r");
	if (!f) {
		writeTemplate(path);
	} else {
		while (fgets(line, sizeof(line), f)) {
			char* eq = strchr(line, '=');
			if (line[0] == ';' || line[0] == '#' || !eq)
				continue;
			*eq = 0;
			char* key = line;
			char* value = eq + 1;
			trim(key);
			trim(value);
			if (!_stricmp(key, "source") && *value && c->sourceCount < MAX_SOURCES)
				snprintf(c->sources[c->sourceCount++], PATH_LEN, "%s", value);
			else if (!_stricmp(key, "token"))
				snprintf(c->token, sizeof(c->token), "%s", value);
			else if (!_stricmp(key, "check"))
				c->check = atoi(value);
			else if (!_stricmp(key, "ask"))
				c->ask = atoi(value);
		}
		fclose(f);
	}
	joinPath(path, sizeof(path), exeDir, "update-state.txt");
	f = openFile(path, "r");
	if (f) {
		while (fgets(line, sizeof(line), f))
			if (!strncmp(line, "skip=", 5)) {
				snprintf(c->skip, sizeof(c->skip), "%s", line + 5);
				trim(c->skip);
			} else if (!strncmp(line, "installed=", 10)) {
				snprintf(c->installed, sizeof(c->installed), "%s", line + 10);
				trim(c->installed);
			}
		fclose(f);
	}
}

static void writeState(const char* skip, const char* installed) {
	char path[PATH_LEN];
	joinPath(path, sizeof(path), exeDir, "update-state.txt");
	FILE* f = openFile(path, "w");
	if (f) {
		fprintf(f, "skip=%s\ninstalled=%s\n", skip, installed);
		fclose(f);
	}
}

// ---- finding builds ----

static void consider(struct Candidate* best, const struct Candidate* c) {
	if (!best->location[0] || compareVersions(&c->version, &best->version) > 0)
		*best = *c;
}

static void scanFolder(const char* dir, struct Candidate* best) {
	char pattern[PATH_LEN];
	joinPath(pattern, sizeof(pattern), dir, ZIP_PREFIX "*.zip");
	wchar_t* w = widen(pattern);
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(w, &fd);
	free(w);
	if (h == INVALID_HANDLE_VALUE)
		return;
	do {
		char name[PATH_LEN];
		struct Candidate c = { 0 };
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		narrow(fd.cFileName, name, sizeof(name));
		if (!parseVersion(name + strlen(ZIP_PREFIX), &c.version, c.versionText, sizeof(c.versionText)))
			continue;
		joinPath(c.location, sizeof(c.location), dir, name);
		snprintf(c.from, sizeof(c.from), "%s", dir);
		say("found %s (version %s)", c.location, c.versionText);
		consider(best, &c);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

static void downloadsFolder(char* out, size_t size) {
	static const GUID downloads = { 0x374DE290, 0x123F, 0x4565, { 0x91, 0x64, 0x39, 0xC4, 0x92, 0x5E, 0x46, 0x7B } };
	PWSTR w = NULL;
	out[0] = 0;
	if (SHGetKnownFolderPath(&downloads, 0, NULL, &w) == S_OK)
		narrow(w, out, size);
	CoTaskMemFree(w);
}

// ---- HTTP (WinHTTP, so Windows' own certificates and proxy settings apply) ----

typedef void (*ProgressFn)(unsigned long long done, unsigned long long total);

struct Buffer {
	char* data;
	size_t size;
};

// Fetches url into buf (when non-NULL) or the file out. Redirects are followed by
// hand so the GitHub token is only ever sent to the host it was meant for.
static int httpGet(const char* url, const char* token, const char* accept, struct Buffer* buf, FILE* out,
                   ProgressFn progress) {
	char current[2048];
	wchar_t tokenHost[256] = L"";
	int ok = 0;
	snprintf(current, sizeof(current), "%s", url);
	HINTERNET session = WinHttpOpen(L"PicoriSeal-Updater/" PICORI_VERSION, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
	                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
		return 0;
	WinHttpSetTimeouts(session, 4000, 4000, 8000, 15000);
	for (int hop = 0; hop < 6; hop++) {
		wchar_t* wurl = widen(current);
		wchar_t host[256], path[2048];
		URL_COMPONENTS u = { .dwStructSize = sizeof(u), .dwHostNameLength = (DWORD) -1,
		                     .dwUrlPathLength = (DWORD) -1, .dwSchemeLength = (DWORD) -1 };
		wchar_t extra[2048];
		BOOL cracked = WinHttpCrackUrl(wurl, 0, 0, &u) && u.dwHostNameLength < 256;
		if (cracked) {
			// The path runs to the end of the URL, so the query (S3 signatures) comes along
			wcsncpy(host, u.lpszHostName, u.dwHostNameLength);
			host[u.dwHostNameLength] = 0;
			swprintf(path, 2048, L"%ls", u.dwUrlPathLength ? u.lpszUrlPath : L"/");
		}
		free(wurl);
		if (!cracked) {
			say("bad URL: %s", current);
			break;
		}
		if (hop == 0)
			wcscpy(tokenHost, host);
		HINTERNET conn = WinHttpConnect(session, host, u.nPort, 0);
		DWORD flags = u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
		HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", path, NULL, WINHTTP_NO_REFERER,
		                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags) : NULL;
		if (!req) {
			if (conn)
				WinHttpCloseHandle(conn);
			break;
		}
		DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
		WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
		extra[0] = 0;
		if (accept) {
			wchar_t* w = widen(accept);
			swprintf(extra, 2048, L"Accept: %ls\r\n", w);
			free(w);
		}
		if (token && token[0] && !wcscmp(host, tokenHost)) {
			wchar_t* w = widen(token);
			size_t n = wcslen(extra);
			swprintf(extra + n, 2048 - n, L"Authorization: Bearer %ls\r\nX-GitHub-Api-Version: 2022-11-28\r\n", w);
			free(w);
		}
		BOOL sent = WinHttpSendRequest(req, extra[0] ? extra : WINHTTP_NO_ADDITIONAL_HEADERS,
		                               extra[0] ? (DWORD) -1 : 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
		            WinHttpReceiveResponse(req, NULL);
		DWORD status = 0, len = sizeof(status);
		if (sent)
			WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &len,
			                    NULL);
		say("GET %s -> %lu", current, (unsigned long) status);
		if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
			wchar_t location[2048];
			len = sizeof(location);
			int have = WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, NULL, location, &len, NULL);
			WinHttpCloseHandle(req);
			WinHttpCloseHandle(conn);
			if (!have)
				break;
			if (location[0] == L'/') {
				char rel[2048], base[2048];
				narrow(location, rel, sizeof(rel));
				char* slash = strstr(current, "://");
				slash = slash ? strchr(slash + 3, '/') : NULL;
				if (slash)
					*slash = 0;
				snprintf(base, sizeof(base), "%s%s", current, rel);
				snprintf(current, sizeof(current), "%s", base);
			} else {
				narrow(location, current, sizeof(current));
			}
			continue;
		}
		if (status == 200) {
			unsigned long long total = 0, done = 0;
			wchar_t lenText[32];
			len = sizeof(lenText);
			if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, NULL, lenText, &len, NULL))
				total = _wcstoui64(lenText, NULL, 10);
			ok = 1;
			for (;;) {
				char chunk[65536];
				DWORD got = 0;
				if (!WinHttpReadData(req, chunk, sizeof(chunk), &got)) {
					ok = 0;
					break;
				}
				if (!got)
					break;
				if (buf) {
					if (buf->size + got > 4 * 1024 * 1024) { // manifests and API answers are small
						ok = 0;
						break;
					}
					buf->data = realloc(buf->data, buf->size + got + 1);
					memcpy(buf->data + buf->size, chunk, got);
					buf->size += got;
					buf->data[buf->size] = 0;
				}
				if (out && fwrite(chunk, 1, got, out) != got) {
					ok = 0;
					break;
				}
				done += got;
				if (progress)
					progress(done, total);
			}
			if (total && done != total)
				ok = 0;
		}
		WinHttpCloseHandle(req);
		WinHttpCloseHandle(conn);
		break;
	}
	WinHttpCloseHandle(session);
	return ok;
}

// ---- tiny JSON helpers (enough for latest.json and GitHub's release answer) ----

// Copies the string value of "key" found after from, or returns NULL.
static const char* jsonString(const char* from, const char* key, char* out, size_t size) {
	char needle[64];
	snprintf(needle, sizeof(needle), "\"%s\"", key);
	const char* p = strstr(from, needle);
	if (!p)
		return NULL;
	p += strlen(needle);
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	if (*p++ != ':')
		return NULL;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	if (*p++ != '"')
		return NULL;
	size_t n = 0;
	for (; *p && *p != '"'; p++) {
		if (*p == '\\' && p[1])
			p++;
		if (n + 1 < size)
			out[n++] = *p;
	}
	out[n] = 0;
	return *p ? p + 1 : NULL;
}

static int isBuildName(const char* name) {
	size_t n = strlen(name);
	return !_strnicmp(name, ZIP_PREFIX, strlen(ZIP_PREFIX)) && n > 4 && !_stricmp(name + n - 4, ".zip");
}

static void checkManifest(const char* url, const char* token, struct Candidate* best) {
	struct Buffer b = { 0 };
	struct Candidate c = { 0 };
	char version[32], file[PATH_LEN];
	if (!httpGet(url, token, "application/json", &b, NULL, NULL) || !b.data) {
		say("could not read %s", url);
		free(b.data);
		return;
	}
	if (jsonString(b.data, "version", version, sizeof(version)) &&
	    parseVersion(version, &c.version, c.versionText, sizeof(c.versionText)) &&
	    (jsonString(b.data, "url", file, sizeof(file)) || jsonString(b.data, "file", file, sizeof(file)))) {
		if (strstr(file, "://")) {
			snprintf(c.location, sizeof(c.location), "%s", file);
		} else { // relative to the manifest
			snprintf(c.location, sizeof(c.location), "%s", url);
			char* slash = strrchr(c.location, '/');
			if (slash)
				snprintf(slash + 1, sizeof(c.location) - (size_t) (slash + 1 - c.location), "%s", file);
		}
		jsonString(b.data, "sha256", c.sha256, sizeof(c.sha256));
		snprintf(c.from, sizeof(c.from), "%s", url);
		c.remote = 1;
		c.githubAsset = token && token[0];
		say("manifest %s offers %s", url, c.versionText);
		consider(best, &c);
	}
	free(b.data);
}

static void checkGitHub(const char* repo, const char* token, struct Candidate* best) {
	char url[PATH_LEN], tag[64];
	struct Buffer b = { 0 };
	const char* api = getenv("PICORI_GITHUB_API"); // for testing against a stand-in server
	snprintf(url, sizeof(url), "%s/repos/%s/releases/latest", api ? api : "https://api.github.com", repo);
	if (!httpGet(url, token, "application/vnd.github+json", &b, NULL, NULL) || !b.data) {
		say("could not read the latest release of %s", repo);
		free(b.data);
		return;
	}
	struct Candidate c = { 0 };
	if (jsonString(b.data, "tag_name", tag, sizeof(tag)) &&
	    parseVersion(tag, &c.version, c.versionText, sizeof(c.versionText))) {
		// Each asset is an object starting with its API "url"; the download is that
		// URL with Accept: application/octet-stream, which works for private repos.
		const char* assets = strstr(b.data, "\"assets\"");
		const char* p = assets;
		char assetUrl[PATH_LEN], name[PATH_LEN];
		while (p && (p = jsonString(p, "url", assetUrl, sizeof(assetUrl)))) {
			if (!strstr(assetUrl, "/releases/assets/"))
				continue;
			const char* next = strstr(p, "/releases/assets/");
			const char* q = jsonString(p, "name", name, sizeof(name));
			if (!q || (next && q > next) || !isBuildName(name))
				continue;
			char digest[80] = "";
			const char* d = jsonString(p, "digest", digest, sizeof(digest));
			if (d && (!next || d < next) && !strncmp(digest, "sha256:", 7))
				snprintf(c.sha256, sizeof(c.sha256), "%s", digest + 7);
			snprintf(c.location, sizeof(c.location), "%s", assetUrl);
			snprintf(c.from, sizeof(c.from), "GitHub (%s)", repo);
			c.remote = 1;
			c.githubAsset = 1;
			say("release %s offers %s", repo, c.versionText);
			consider(best, &c);
			break;
		}
	}
	free(b.data);
}

// ---- checking and unpacking ----

static int sha256File(const char* path, char* hex) {
	BCRYPT_ALG_HANDLE alg;
	BCRYPT_HASH_HANDLE hash;
	unsigned char digest[32];
	int ok = 0;
	FILE* f = openFile(path, "rb");
	if (!f)
		return 0;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0) {
		if (BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) == 0) {
			unsigned char chunk[65536];
			size_t n;
			while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
				BCryptHashData(hash, chunk, (ULONG) n, 0);
			ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
			BCryptDestroyHash(hash);
		}
		BCryptCloseAlgorithmProvider(alg, 0);
	}
	fclose(f);
	for (int i = 0; ok && i < 32; i++)
		sprintf(hex + i * 2, "%02x", digest[i]);
	return ok;
}

// A GBA cartridge header has 0x96 at 0xB2 and a checksum over 0xA0-0xBC at 0xBD.
static int looksLikeGame(const unsigned char* rom, size_t size) {
	if (size < 0x200 || rom[0xB2] != 0x96)
		return 0;
	unsigned char sum = 0;
	for (int i = 0xA0; i <= 0xBC; i++)
		sum -= rom[i];
	sum -= 0x19;
	return sum == rom[0xBD];
}

// ---- zip reading (zlib's inflate; the Windows libmgba is built without zlib) ----

static unsigned rd16(const unsigned char* p) {
	return p[0] | p[1] << 8;
}

static unsigned rd32(const unsigned char* p) {
	return p[0] | p[1] << 8 | p[2] << 16 | (unsigned) p[3] << 24;
}

static unsigned char* readWhole(const char* path, size_t* size) {
	FILE* f = openFile(path, "rb");
	unsigned char* data = NULL;
	if (!f)
		return NULL;
	if (!_fseeki64(f, 0, SEEK_END)) {
		long long n = _ftelli64(f);
		if (n > 0 && n < 256 * 1024 * 1024 && !_fseeki64(f, 0, SEEK_SET)) {
			data = malloc((size_t) n);
			if (data && fread(data, 1, (size_t) n, f) != (size_t) n) {
				free(data);
				data = NULL;
			}
			*size = (size_t) n;
		}
	}
	fclose(f);
	return data;
}

// Returns the entry's bytes (malloc'd), checked against its CRC, or NULL.
static unsigned char* zipExtract(const unsigned char* zip, size_t zipSize, const unsigned char* cd, size_t* outSize) {
	unsigned method = rd16(cd + 10), crc = rd32(cd + 16), packed = rd32(cd + 20), size = rd32(cd + 24);
	unsigned offset = rd32(cd + 42);
	if (offset + 30 > zipSize || rd32(zip + offset) != 0x04034b50)
		return NULL;
	size_t dataAt = (size_t) offset + 30 + rd16(zip + offset + 26) + rd16(zip + offset + 28);
	if (dataAt + packed > zipSize || size > 256 * 1024 * 1024)
		return NULL;
	unsigned char* out = malloc(size ? size : 1);
	if (!out)
		return NULL;
	int ok = 0;
	if (method == 0 && packed == size) {
		memcpy(out, zip + dataAt, size);
		ok = 1;
	} else if (method == 8) {
		z_stream z = { 0 };
		if (inflateInit2(&z, -MAX_WBITS) == Z_OK) {
			z.next_in = (Bytef*) (zip + dataAt);
			z.avail_in = packed;
			z.next_out = out;
			z.avail_out = size;
			ok = inflate(&z, Z_FINISH) == Z_STREAM_END && z.total_out == size;
			inflateEnd(&z);
		}
	}
	if (!ok || crc32(0, out, size) != crc) {
		free(out);
		return NULL;
	}
	*outSize = size;
	return out;
}

struct Entry {
	char name[260];
};

static int allowedName(const char* name) {
	if (!name[0] || strchr(name, '/') || strchr(name, '\\') || strchr(name, ':') || name[0] == '.')
		return 0;
	// Never let a build overwrite settings or saves
	return _stricmp(name, "update.ini") && _stricmp(name, "update-state.txt") && _stricmp(name, "player1.sav") &&
	       _stricmp(name, "player2.sav");
}

static const char* exeName(void) {
	const char* slash = strrchr(exePath, '\\');
	return slash ? slash + 1 : exePath;
}

// Unpacks the build's files next to the game as NAME.new. Files sit either at the
// zip's top or in one folder (picori-seal-vX.Y/); anything deeper is ignored.
static int unpack(const char* zipPath, struct Entry* entries, int* count, char* error, size_t errorSize) {
	size_t zipSize = 0;
	unsigned char* zip = readWhole(zipPath, &zipSize);
	int haveExe = 0, haveGame = 0, ok = 1;
	*count = 0;
	if (!zip) {
		snprintf(error, errorSize, "Could not read the update.");
		return 0;
	}
	// Find the end-of-central-directory record
	const unsigned char* eocd = NULL;
	for (size_t i = zipSize >= 22 ? zipSize - 22 : 0; zipSize >= 22; i--) {
		if (rd32(zip + i) == 0x06054b50) {
			eocd = zip + i;
			break;
		}
		if (i == 0 || zipSize - i > 22 + 65535)
			break;
	}
	if (!eocd || rd32(eocd + 16) + (size_t) rd32(eocd + 12) > zipSize) {
		snprintf(error, errorSize, "The update is not a readable zip.");
		free(zip);
		return 0;
	}
	const unsigned char* cd = zip + rd32(eocd + 16);
	unsigned total = rd16(eocd + 10);
	for (unsigned n = 0; ok && n < total && *count < 32; n++) {
		if (cd + 46 > zip + zipSize || rd32(cd) != 0x02014b50) {
			snprintf(error, errorSize, "The update is not a readable zip.");
			ok = 0;
			break;
		}
		unsigned nameLen = rd16(cd + 28), extraLen = rd16(cd + 30), commentLen = rd16(cd + 32);
		char full[PATH_LEN];
		if (cd + 46 + nameLen > zip + zipSize || nameLen >= sizeof(full)) {
			ok = 0;
			break;
		}
		memcpy(full, cd + 46, nameLen);
		full[nameLen] = 0;
		const unsigned char* here = cd;
		cd += 46 + nameLen + extraLen + commentLen;
		const char* base = full;
		const char* slash = strchr(full, '/');
		if (!nameLen || full[nameLen - 1] == '/')
			continue;
		if (slash) {
			base = slash + 1;
			if (strchr(base, '/') || !strncmp(full, "..", 2))
				continue;
		}
		if (!allowedName(base))
			continue;
		size_t size = 0;
		unsigned char* data = zipExtract(zip, zipSize, here, &size);
		if (!data) {
			snprintf(error, errorSize, "Could not unpack %s from the update; the zip may be damaged.", base);
			ok = 0;
			break;
		}
		if (!_stricmp(base, "zelda-blend.gba")) {
			haveGame = looksLikeGame(data, size);
			if (!haveGame) {
				snprintf(error, errorSize, "The game in the update is damaged.");
				ok = 0;
			}
		}
		if (!_stricmp(base, "zelda-blend.exe")) {
			haveExe = size > 2 && data[0] == 'M' && data[1] == 'Z';
			if (!haveExe) {
				snprintf(error, errorSize, "The program in the update is damaged.");
				ok = 0;
			}
		}
		struct Entry* en = &entries[(*count)++];
		snprintf(en->name, sizeof(en->name), "%s", base);
		char path[PATH_LEN];
		joinPath(path, sizeof(path), exeDir, base);
		strncat(path, ".new", sizeof(path) - strlen(path) - 1);
		FILE* f = ok ? openFile(path, "wb") : NULL;
		int written = f && fwrite(data, 1, size, f) == size;
		if (f && fclose(f))
			written = 0;
		free(data);
		if (ok && !written) {
			snprintf(error, errorSize, "Could not write %s next to the game.", base);
			ok = 0;
		}
	}
	free(zip);
	if (ok && !haveExe && !haveGame) {
		snprintf(error, errorSize, "The zip has no zelda-blend.exe or zelda-blend.gba in it.");
		ok = 0;
	}
	return ok;
}

static void dropNewFiles(const struct Entry* entries, int count) {
	for (int i = 0; i < count; i++) {
		char path[PATH_LEN];
		joinPath(path, sizeof(path), exeDir, entries[i].name);
		strncat(path, ".new", sizeof(path) - strlen(path) - 1);
		deleteFile(path);
	}
}

// Moves the unpacked files into place. The running program can't be overwritten
// but can be renamed, so it becomes NAME.old and is deleted on the next start.
static int install(const struct Entry* entries, int count, char* error, size_t errorSize) {
	char previous[PATH_LEN], target[PATH_LEN], staged[PATH_LEN], backup[PATH_LEN];
	static const char* saves[] = { "player1.sav", "player2.sav" };
	joinPath(previous, sizeof(previous), exeDir, "previous");
	makeDir(previous);
	for (int i = 0; i < 2; i++) {
		joinPath(target, sizeof(target), exeDir, saves[i]);
		joinPath(backup, sizeof(backup), previous, saves[i]);
		if (fileExists(target))
			copyFile(target, backup);
	}
	for (int i = 0; i < count; i++) {
		const char* name = !_stricmp(entries[i].name, "zelda-blend.exe") ? exeName() : entries[i].name;
		joinPath(target, sizeof(target), exeDir, name);
		joinPath(backup, sizeof(backup), previous, name);
		if (fileExists(target))
			copyFile(target, backup);
	}
	char old[PATH_LEN];
	snprintf(old, sizeof(old), "%s.old", exePath);
	deleteFile(old);
	for (int i = 0; i < count; i++) {
		int isExe = !_stricmp(entries[i].name, "zelda-blend.exe");
		joinPath(staged, sizeof(staged), exeDir, entries[i].name);
		strncat(staged, ".new", sizeof(staged) - strlen(staged) - 1);
		joinPath(target, sizeof(target), exeDir, entries[i].name);
		int moved = isExe ? moveFile(exePath, old) && moveFile(staged, exePath) : moveFile(staged, target);
		if (moved)
			continue;
		// Put back what was already replaced, from the backups made above
		snprintf(error, errorSize, "Could not replace %s. Is it open in another program?",
		         isExe ? exeName() : entries[i].name);
		for (int j = 0; j <= i; j++) {
			if (!_stricmp(entries[j].name, "zelda-blend.exe")) {
				if (fileExists(old))
					moveFile(old, exePath);
			} else if (j < i) {
				joinPath(target, sizeof(target), exeDir, entries[j].name);
				joinPath(backup, sizeof(backup), previous, entries[j].name);
				if (fileExists(backup))
					copyFile(backup, target);
				else
					deleteFile(target);
			}
		}
		dropNewFiles(entries, count);
		return 0;
	}
	return 1;
}

// ---- progress window ----

static SDL_Window* progressWin;
static SDL_Renderer* progressRen;
static Uint32 progressLast;

static void progressOpen(void) {
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
		return;
	progressWin = SDL_CreateWindow("Updating " TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 420, 48, 0);
	progressRen = progressWin ? SDL_CreateRenderer(progressWin, -1, 0) : NULL;
}

static void progressDraw(unsigned long long done, unsigned long long total) {
	if (!progressRen || SDL_GetTicks() - progressLast < 30)
		return;
	progressLast = SDL_GetTicks();
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
	}
	int pct = total ? (int) (done * 100 / total) : 0;
	char title[96];
	snprintf(title, sizeof(title), "Downloading the update... %d%%", pct);
	SDL_SetWindowTitle(progressWin, title);
	SDL_SetRenderDrawColor(progressRen, 24, 32, 24, 255);
	SDL_RenderClear(progressRen);
	SDL_Rect frame = { 12, 14, 396, 20 }, bar = { 14, 16, 392 * pct / 100, 16 };
	SDL_SetRenderDrawColor(progressRen, 200, 184, 120, 255);
	SDL_RenderDrawRect(progressRen, &frame);
	SDL_SetRenderDrawColor(progressRen, 96, 176, 72, 255);
	SDL_RenderFillRect(progressRen, &bar);
	SDL_RenderPresent(progressRen);
}

static void progressClose(void) {
	if (progressRen)
		SDL_DestroyRenderer(progressRen);
	if (progressWin)
		SDL_DestroyWindow(progressWin);
	progressRen = NULL;
	progressWin = NULL;
}

// ---- main flow ----

static void showError(const char* msg) {
	say("error: %s", msg);
	if (!verbose)
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, TITLE " update", msg, NULL);
}

static void cleanupOld(void) {
	char old[PATH_LEN];
	snprintf(old, sizeof(old), "%s.old", exePath);
	for (int i = 0; i < 10 && fileExists(old); i++) {
		deleteFile(old);
		if (fileExists(old))
			Sleep(200); // the previous version may still be closing
	}
}

static void relaunch(void) {
	wchar_t* exe = widen(exePath);
	wchar_t* dir = widen(exeDir);
	wchar_t* cmd = _wcsdup(GetCommandLineW());
	STARTUPINFOW si = { .cb = sizeof(si) };
	PROCESS_INFORMATION pi;
	if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
	free(exe);
	free(dir);
	free(cmd);
}

int updaterRun(const struct UpdaterOptions* opts) {
	wchar_t wpath[PATH_LEN];
	struct Config cfg;
	struct Candidate best = { 0 };
	struct Version mine;
	char dir[PATH_LEN];

	verbose = opts->onlyUpdate;
	GetModuleFileNameW(NULL, wpath, PATH_LEN);
	narrow(wpath, exePath, sizeof(exePath));
	snprintf(exeDir, sizeof(exeDir), "%s", exePath);
	char* slash = strrchr(exeDir, '\\');
	if (slash)
		*slash = 0;
	cleanupOld();
	readConfig(&cfg);
	if (!cfg.check && !opts->source) {
		say("update checks are off");
		return 0;
	}
	if (!parseVersion(PICORI_VERSION, &mine, NULL, 0)) {
		say("this is a development build, so it does not update itself");
		if (!opts->source)
			return 0;
	}
	say("this is version %s", PICORI_VERSION);

	joinPath(dir, sizeof(dir), exeDir, "updates");
	scanFolder(dir, &best);
	downloadsFolder(dir, sizeof(dir));
	if (dir[0])
		scanFolder(dir, &best);
	const char* sources[MAX_SOURCES + 1];
	int n = 0;
	for (int i = 0; i < cfg.sourceCount; i++)
		sources[n++] = cfg.sources[i];
	if (opts->source)
		sources[n++] = opts->source;
	for (int i = 0; i < n; i++) {
		if (!_strnicmp(sources[i], "github:", 7))
			checkGitHub(sources[i] + 7, cfg.token, &best);
		else if (!_strnicmp(sources[i], "http://", 7) || !_strnicmp(sources[i], "https://", 8))
			checkManifest(sources[i], cfg.token, &best);
		else if (fileExists(sources[i])) { // a zip given directly
			struct Candidate c = { 0 };
			const char* base = strrchr(sources[i], '\\');
			base = base ? base + 1 : sources[i];
			if (!_strnicmp(base, ZIP_PREFIX, strlen(ZIP_PREFIX)) &&
			    parseVersion(base + strlen(ZIP_PREFIX), &c.version, c.versionText, sizeof(c.versionText))) {
				snprintf(c.location, sizeof(c.location), "%s", sources[i]);
				snprintf(c.from, sizeof(c.from), "%s", sources[i]);
				consider(&best, &c);
			}
		} else
			scanFolder(sources[i], &best);
	}

	if (!best.location[0] || compareVersions(&best.version, &mine) <= 0) {
		say("no newer version found");
		return 0;
	}
	// A zip named for a newer version than the program inside it would otherwise be
	// offered again on every start
	if (!strcmp(cfg.installed, best.versionText)) {
		say("version %s was already installed from %s, but this program says %s", best.versionText, best.from,
		    PICORI_VERSION);
		return 0;
	}
	if (cfg.skip[0] && !strcmp(cfg.skip, best.versionText) && !opts->assumeYes) {
		say("version %s was skipped", best.versionText);
		return 0;
	}
	say("version %s is newer, from %s", best.versionText, best.from);

	if (cfg.ask && !opts->assumeYes) {
		char msg[PATH_LEN + 512];
		snprintf(msg, sizeof(msg),
		         "Version %s of " TITLE " is ready (you have %s).\n\nFound in: %s\n\n"
		         "Install it now? Your saves are kept, and this version is backed up in the \"previous\" folder.",
		         best.versionText, PICORI_VERSION, best.from);
		const SDL_MessageBoxButtonData buttons[] = {
			{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Install" },
			{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Later" },
			{ 0, 2, "Skip this version" },
		};
		const SDL_MessageBoxData box = { SDL_MESSAGEBOX_INFORMATION, NULL, TITLE " update", msg, 3, buttons, NULL };
		int choice = 0;
		if (SDL_ShowMessageBox(&box, &choice) != 0)
			choice = 0;
		if (choice == 2)
			writeState(best.versionText, cfg.installed);
		if (choice != 1)
			return 0;
	}

	char zipPath[PATH_LEN], error[512] = "";
	int downloaded = 0;
	if (best.remote) {
		char name[64];
		joinPath(dir, sizeof(dir), exeDir, "updates");
		makeDir(dir);
		snprintf(name, sizeof(name), ZIP_PREFIX "%s.zip.part", best.versionText);
		joinPath(zipPath, sizeof(zipPath), dir, name);
		FILE* f = openFile(zipPath, "wb");
		if (!f) {
			showError("Could not save the update next to the game.");
			return 0;
		}
		progressOpen();
		int ok = httpGet(best.location, best.githubAsset ? cfg.token : NULL,
		                 best.githubAsset ? "application/octet-stream" : NULL, NULL, f, progressDraw);
		progressClose();
		if (fclose(f))
			ok = 0;
		if (!ok) {
			deleteFile(zipPath);
			showError("The update could not be downloaded. The game will start as it is; it will try again next time.");
			return 0;
		}
		downloaded = 1;
	} else {
		snprintf(zipPath, sizeof(zipPath), "%s", best.location);
	}

	if (best.sha256[0]) {
		char hex[65];
		if (!sha256File(zipPath, hex) || _stricmp(hex, best.sha256)) {
			if (downloaded)
				deleteFile(zipPath);
			showError("The update did not match its checksum, so it was not installed.");
			return 0;
		}
		say("checksum matches");
	}

	struct Entry entries[32];
	int count = 0;
	int ok = unpack(zipPath, entries, &count, error, sizeof(error));
	if (ok)
		ok = install(entries, count, error, sizeof(error));
	else
		dropNewFiles(entries, count);
	if (downloaded)
		deleteFile(zipPath);
	if (!ok) {
		char msg[640];
		snprintf(msg, sizeof(msg), "The update to version %s was not installed. %s", best.versionText, error);
		showError(msg);
		return 0;
	}
	writeState(cfg.skip, best.versionText);
	say("installed version %s", best.versionText);
	if (opts->onlyUpdate)
		return 1;
	relaunch();
	return 1;
}

#endif
