package com.picoriseal.game;

// Auto updater for the Android app, the counterpart of blend/host/updater.c.
//
// Builds hold the player's own ROM, so they never come from a public server.
// The app finds a newer picori-seal-vX.Y.zip (the same zip as Windows) or
// picori-seal-vX.Y.apk:
//   - opened with the app ("Open with" on a download),
//   - in a folder the player picked once (Android won't let apps read Download/),
//   - from a private source: the URL of a latest.json manifest (package.sh), or
//     github:owner/repo for the latest release of a private repo, with a token.
// A zip updates the game itself: its zelda-blend.gba goes into the app's files
// folder, where the host loads it in place of the ROM inside the APK. An APK
// (only for host changes) goes to Android's installer. Saves stay where they are,
// and the old game file and saves are copied to files/previous/ first.
//
// Hooks for the activity: prepare() before super.onCreate, then check() and
// handleIntent(); handleIntent() in onNewIntent; forward onActivityResult.
// Settings: the "Update settings" app shortcut, or the intent extra "updateSettings".

import android.app.Activity;
import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageInstaller;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.text.InputType;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public final class Updater {
	public static final int REQUEST_FOLDER = 0x5EA1;

	static final String GAME = "zelda-blend.gba";
	private static final String TITLE = "Zelda: The Picori Seal";
	private static final String PREFS = "updater";
	private static final String[] SAVES = { "player1.sav", "player2.sav" };
	static final Pattern BUILD_NAME = Pattern.compile("picori-seal-v(\\d+(?:\\.\\d+){0,2})[^/]*\\.(zip|apk)",
			Pattern.CASE_INSENSITIVE);

	private Updater() {
	}

	static final class Candidate {
		String version;
		boolean apk;     // an APK to install, else a game zip
		Uri localUri;    // a file on the device
		String url;      // or one to download
		String sha256;   // lowercase hex, or null
		boolean useToken;
		String from;
	}

	// ---- versions ----

	static int[] parse(String v) {
		int[] p = new int[3];
		if (v == null)
			return p;
		Matcher m = Pattern.compile("(\\d+)(?:\\.(\\d+))?(?:\\.(\\d+))?").matcher(v);
		if (m.find())
			for (int i = 0; i < 3; i++)
				p[i] = m.group(i + 1) == null ? 0 : Integer.parseInt(m.group(i + 1));
		return p;
	}

	static int compare(String a, String b) {
		int[] x = parse(a), y = parse(b);
		for (int i = 0; i < 3; i++)
			if (x[i] != y[i])
				return x[i] < y[i] ? -1 : 1;
		return 0;
	}

	static String apkVersion(Context c) {
		try {
			return c.getPackageManager().getPackageInfo(c.getPackageName(), 0).versionName;
		} catch (Exception e) {
			return "0";
		}
	}

	/** The game's version: the APK's, or a newer game file installed from a zip. */
	static String gameVersion(Context c) {
		String apk = apkVersion(c);
		String content = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("gameVersion", null);
		return content != null && compare(content, apk) > 0 ? content : apk;
	}

	// ---- hooks ----

	/** Before super.onCreate: drops a game file older than the APK's own. */
	public static void prepare(Activity a) {
		SharedPreferences prefs = a.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
		String content = prefs.getString("gameVersion", null);
		File dir = a.getExternalFilesDir(null);
		if (dir == null)
			return;
		new File(dir, GAME + ".new").delete();
		if (content != null && compare(content, apkVersion(a)) <= 0) {
			new File(dir, GAME).delete();
			prefs.edit().remove("gameVersion").apply();
		}
	}

	/** After super.onCreate: looks for a newer build in the background. */
	public static void check(final Activity a) {
		if (a.getIntent() != null && a.getIntent().getBooleanExtra("updateSettings", false)) {
			showSettings(a);
			return;
		}
		final SharedPreferences prefs = a.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
		if (!prefs.getBoolean("check", true))
			return;
		new Thread(() -> {
			Candidate best = null;
			String token = prefs.getString("token", "").trim();
			best = newer(a, best, scanFolder(a, prefs.getString("folder", null)));
			try {
				String source = prefs.getString("source", "").trim();
				if (source.startsWith("github:"))
					best = newer(a, best, checkGitHub(a, source.substring(7), token));
				else if (source.startsWith("http://") || source.startsWith("https://"))
					best = newer(a, best, checkManifest(a, source, token));
			} catch (Exception e) {
				// No network or a bad source: play the installed version
			}
			if (best == null || best.version.equals(prefs.getString("skip", "")))
				return;
			final Candidate found = best;
			a.runOnUiThread(() -> offer(a, found, true));
		}, "updater").start();
	}

	/** In onCreate and onNewIntent: a zip or APK opened with the app. */
	public static void handleIntent(Activity a, Intent intent) {
		if (intent == null || !Intent.ACTION_VIEW.equals(intent.getAction()) || intent.getData() == null)
			return;
		Uri uri = intent.getData();
		String name = displayName(a, uri);
		Candidate c = new Candidate();
		Matcher m = BUILD_NAME.matcher(name != null ? name : "");
		c.localUri = uri;
		c.from = name != null ? name : "the opened file";
		if (m.find()) {
			c.version = m.group(1);
			c.apk = m.group(2).equalsIgnoreCase("apk");
		} else {
			String type = a.getContentResolver().getType(uri);
			c.apk = "application/vnd.android.package-archive".equals(type);
			c.version = null;
		}
		intent.setData(null); // don't offer it again when the activity is recreated
		offer(a, c, false);
	}

	public static boolean onActivityResult(Activity a, int requestCode, int resultCode, Intent data) {
		if (requestCode != REQUEST_FOLDER)
			return false;
		if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
			Uri tree = data.getData();
			a.getContentResolver().takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
			a.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString("folder", tree.toString()).apply();
			check(a);
		}
		return true;
	}

	// Only builds newer than what is installed count: APKs against the APK, zips
	// against the game.
	private static Candidate newer(Context ctx, Candidate best, Candidate c) {
		if (c == null || c.version == null)
			return best;
		if (compare(c.version, c.apk ? apkVersion(ctx) : gameVersion(ctx)) <= 0)
			return best;
		if (best == null || compare(c.version, best.version) > 0 || compare(c.version, best.version) == 0 && c.apk)
			return c; // a same-version APK wins: it carries the game too
		return best;
	}

	// ---- sources ----

	private static String displayName(Context c, Uri uri) {
		if ("file".equals(uri.getScheme()))
			return uri.getLastPathSegment();
		try (Cursor cur = c.getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null,
				null, null)) {
			if (cur != null && cur.moveToFirst())
				return cur.getString(0);
		} catch (Exception e) {
			// fall through
		}
		return uri.getLastPathSegment();
	}

	private static Candidate scanFolder(Context c, String tree) {
		if (tree == null)
			return null;
		Candidate best = null;
		try {
			Uri treeUri = Uri.parse(tree);
			Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri,
					DocumentsContract.getTreeDocumentId(treeUri));
			try (Cursor cur = c.getContentResolver().query(children, new String[] {
					DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME },
					null, null, null)) {
				while (cur != null && cur.moveToNext()) {
					Matcher m = BUILD_NAME.matcher(cur.getString(1));
					if (!m.matches())
						continue;
					Candidate cand = new Candidate();
					cand.version = m.group(1);
					cand.apk = m.group(2).equalsIgnoreCase("apk");
					cand.localUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, cur.getString(0));
					cand.from = "your updates folder";
					best = newer(c, best, cand);
				}
			}
		} catch (Exception e) {
			return null; // folder gone or permission dropped
		}
		return best;
	}

	static Candidate checkManifest(Context ctx, String url, String token) throws Exception {
		String json = new String(get(url, token, "application/json", null, null), "UTF-8");
		String version = jsonString(json, "version", 0);
		if (version == null)
			return null;
		String base = url.substring(0, url.lastIndexOf('/') + 1);
		Candidate best = null;
		String file = jsonString(json, "file", 0);
		if (file == null)
			file = jsonString(json, "url", 0);
		if (file != null) {
			Candidate c = new Candidate();
			c.version = version;
			c.url = file.contains("://") ? file : base + file;
			c.sha256 = jsonString(json, "sha256", 0);
			c.useToken = !token.isEmpty();
			c.from = url;
			best = newer(ctx, best, c);
		}
		String apk = jsonString(json, "apk", 0);
		if (apk != null) {
			Candidate c = new Candidate();
			String v = jsonString(json, "apk_version", 0);
			c.version = v != null ? v : version;
			c.apk = true;
			c.url = apk.contains("://") ? apk : base + apk;
			c.sha256 = jsonString(json, "apk_sha256", 0);
			c.useToken = !token.isEmpty();
			c.from = url;
			best = newer(ctx, best, c);
		}
		return best;
	}

	static Candidate checkGitHub(Context ctx, String repo, String token) throws Exception {
		String api = System.getProperty("picori.github.api", "https://api.github.com");
		String json = new String(get(api + "/repos/" + repo + "/releases/latest", token,
				"application/vnd.github+json", null, null), "UTF-8");
		String tag = jsonString(json, "tag_name", 0);
		int at = json.indexOf("\"assets\"");
		if (tag == null || at < 0)
			return null;
		// Each asset starts with its API "url"; that URL with Accept: octet-stream
		// downloads it, private repos included.
		Matcher m = Pattern.compile("\"url\"\\s*:\\s*\"([^\"]*/releases/assets/\\d+)\"").matcher(json);
		Candidate best = null;
		while (m.find(at)) {
			at = m.end();
			int next = json.indexOf("/releases/assets/", at);
			if (next < 0)
				next = json.length();
			int nameAt = json.indexOf("\"name\"", at);
			String name = jsonString(json, "name", at);
			if (name == null || nameAt > next)
				continue;
			Matcher nm = BUILD_NAME.matcher(name);
			if (!nm.matches())
				continue;
			Candidate c = new Candidate();
			c.version = nm.group(1); // the file's own version; the tag may run ahead of an older APK
			c.apk = nm.group(2).equalsIgnoreCase("apk");
			c.url = m.group(1);
			c.useToken = true;
			int digestAt = json.indexOf("\"digest\"", at);
			String digest = jsonString(json, "digest", at);
			if (digest != null && digest.startsWith("sha256:") && digestAt < next)
				c.sha256 = digest.substring(7);
			c.from = "GitHub (" + repo + ")";
			best = newer(ctx, best, c);
		}
		return best;
	}

	static String jsonString(String json, String key, int from) {
		Matcher m = Pattern.compile("\"" + Pattern.quote(key) + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"").matcher(json);
		return m.find(from) ? m.group(1).replace("\\/", "/").replace("\\\"", "\"") : null;
	}

	interface Progress {
		void update(long done, long total);
	}

	// Follows redirects by hand so the token only goes to the host it was meant for.
	static byte[] get(String url, String token, String accept, File out, Progress progress) throws Exception {
		String tokenHost = new URL(url).getHost();
		for (int hop = 0; hop < 6; hop++) {
			URL u = new URL(url);
			HttpURLConnection conn = (HttpURLConnection) u.openConnection();
			try {
				conn.setInstanceFollowRedirects(false);
				conn.setConnectTimeout(5000);
				conn.setReadTimeout(15000);
				conn.setRequestProperty("User-Agent", "PicoriSeal-Updater");
				if (accept != null)
					conn.setRequestProperty("Accept", accept);
				if (token != null && !token.isEmpty() && u.getHost().equals(tokenHost)) {
					conn.setRequestProperty("Authorization", "Bearer " + token);
					conn.setRequestProperty("X-GitHub-Api-Version", "2022-11-28");
				}
				int status = conn.getResponseCode();
				String location = conn.getHeaderField("Location");
				if (status >= 300 && status < 400 && location != null) {
					url = new URL(u, location).toString();
					continue;
				}
				if (status != 200)
					throw new Exception("HTTP " + status);
				long total = conn.getContentLengthLong(), done = 0;
				try (InputStream in = conn.getInputStream();
						OutputStream os = out != null ? new FileOutputStream(out) : new ByteArrayOutputStream()) {
					byte[] buf = new byte[65536];
					int n;
					while ((n = in.read(buf)) > 0) {
						os.write(buf, 0, n);
						done += n;
						if (out == null && done > (4 << 20))
							throw new Exception("answer too large");
						if (progress != null)
							progress.update(done, total);
					}
					if (total > 0 && done != total)
						throw new Exception("download cut short");
					return out == null ? ((ByteArrayOutputStream) os).toByteArray() : null;
				}
			} finally {
				conn.disconnect();
			}
		}
		throw new Exception("too many redirects");
	}

	// ---- asking ----

	private static void offer(final Activity a, final Candidate c, boolean found) {
		final SharedPreferences prefs = a.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
		String what = c.version != null ? "Version " + c.version : "This build";
		String msg = found
				? what + " is ready (you have " + gameVersion(a) + ").\n\nFound in: " + c.from
						+ "\n\nInstall it now? Your saves are kept."
				: "Install " + c.from + "? You have version " + gameVersion(a) + ". Your saves are kept.";
		AlertDialog.Builder b = new AlertDialog.Builder(a).setTitle(TITLE + " update").setMessage(msg)
				.setPositiveButton("Install", (d, w) -> fetchAndInstall(a, c)).setNegativeButton("Later", null);
		if (found)
			b.setNeutralButton("Skip this version", (d, w) -> prefs.edit().putString("skip", c.version).apply());
		b.show();
	}

	private static void fetchAndInstall(final Activity a, final Candidate c) {
		final AlertDialog progress = new AlertDialog.Builder(a).setTitle("Getting the update...").setMessage("")
				.setCancelable(false).show();
		new Thread(() -> {
			String error = null;
			String version = c.version;
			File dir = new File(a.getCacheDir(), "updates");
			File file = new File(dir, c.apk ? "update.apk" : "update.zip");
			try {
				dir.mkdirs();
				if (c.localUri != null) {
					try (InputStream in = a.getContentResolver().openInputStream(c.localUri);
							OutputStream os = new FileOutputStream(file)) {
						copy(in, os);
					}
				} else {
					String token = a.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("token", "").trim();
					get(c.url, c.useToken ? token : null, c.useToken ? "application/octet-stream" : null, file,
							(done, total) -> {
								if (total > 0)
									a.runOnUiThread(() -> progress.setMessage(done * 100 / total + "%"));
							});
				}
				if (c.sha256 != null && !c.sha256.equalsIgnoreCase(sha256(file)))
					error = "The update did not match its checksum, so it was not installed.";
				else if (!c.apk)
					error = installGame(a, file, version);
			} catch (Exception e) {
				error = c.localUri != null ? "The update could not be read."
						: "The update could not be downloaded. The game will start as it is; it will try again next time.";
			}
			if (error != null || !c.apk)
				file.delete();
			final String err = error;
			a.runOnUiThread(() -> {
				progress.dismiss();
				if (err != null)
					new AlertDialog.Builder(a).setTitle(TITLE + " update").setMessage(err)
							.setPositiveButton("OK", null).show();
				else if (c.apk)
					installApk(a, file);
				else
					new AlertDialog.Builder(a).setTitle(TITLE + " update")
							.setMessage("The game is updated. Restart now to play the new version?")
							.setPositiveButton("Restart", (d, w) -> restart(a)).setNegativeButton("Later", null)
							.show();
			});
		}, "updater-download").start();
	}

	private static void copy(InputStream in, OutputStream os) throws Exception {
		byte[] buf = new byte[65536];
		int n;
		while ((n = in.read(buf)) > 0)
			os.write(buf, 0, n);
	}

	// ---- installing a game zip ----

	// A GBA cartridge header has 0x96 at 0xB2 and a checksum over 0xA0-0xBC at 0xBD.
	static boolean looksLikeGame(byte[] rom, int size) {
		if (size < 0x200 || (rom[0xB2] & 0xFF) != 0x96)
			return false;
		int sum = 0;
		for (int i = 0xA0; i <= 0xBC; i++)
			sum -= rom[i] & 0xFF;
		sum -= 0x19;
		return (sum & 0xFF) == (rom[0xBD] & 0xFF);
	}

	/** Puts the zip's zelda-blend.gba in the files folder. Returns an error message or null. */
	static String installGame(Context ctx, File zip, String version) throws Exception {
		File dir = ctx.getExternalFilesDir(null);
		if (dir == null)
			return "The game's files folder isn't available.";
		byte[] rom = null;
		int size = 0;
		String folder = null;
		try (ZipInputStream in = new ZipInputStream(new FileInputStream(zip))) {
			ZipEntry e;
			while ((e = in.getNextEntry()) != null) {
				String name = e.getName();
				int slash = name.indexOf('/');
				String base = slash >= 0 ? name.substring(slash + 1) : name;
				if (slash >= 0 && folder == null)
					folder = name.substring(0, slash);
				if (e.isDirectory() || !base.equalsIgnoreCase(GAME))
					continue;
				ByteArrayOutputStream os = new ByteArrayOutputStream(32 << 20);
				copy(in, os); // ZipInputStream checks the entry's CRC as it reads
				rom = os.toByteArray();
				size = rom.length;
			}
		} catch (java.util.zip.ZipException e) {
			return "The update is damaged.";
		}
		if (rom == null)
			return "The zip has no " + GAME + " in it.";
		if (!looksLikeGame(rom, size))
			return "The game in the update is damaged.";
		if (version == null && folder != null) {
			Matcher m = BUILD_NAME.matcher(folder + ".zip");
			if (m.matches())
				version = m.group(1);
		}
		if (version == null)
			return "This zip doesn't say which version it is (name it picori-seal-vX.Y.zip).";
		// Back up the current game file and saves
		File previous = new File(dir, "previous");
		previous.mkdirs();
		for (String s : SAVES)
			copyFile(new File(dir, s), new File(previous, s));
		copyFile(new File(dir, GAME), new File(previous, GAME));
		File staged = new File(dir, GAME + ".new");
		try (FileOutputStream os = new FileOutputStream(staged)) {
			os.write(rom, 0, size);
			os.getFD().sync();
		}
		if (!staged.renameTo(new File(dir, GAME))) {
			staged.delete();
			return "Could not replace the game file.";
		}
		ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString("gameVersion", version).apply();
		return null;
	}

	private static void copyFile(File from, File to) {
		if (!from.isFile())
			return;
		try (InputStream in = new FileInputStream(from); OutputStream os = new FileOutputStream(to)) {
			copy(in, os);
		} catch (Exception e) {
			// a missing backup doesn't stop the update
		}
	}

	private static void restart(Activity a) {
		Intent i = a.getPackageManager().getLaunchIntentForPackage(a.getPackageName());
		if (i != null)
			a.startActivity(Intent.makeRestartActivityTask(i.getComponent()));
		android.os.Process.killProcess(android.os.Process.myPid());
	}

	// ---- installing an APK ----

	private static void installApk(final Activity a, final File apk) {
		if (Build.VERSION.SDK_INT >= 26 && !a.getPackageManager().canRequestPackageInstalls()) {
			new AlertDialog.Builder(a).setTitle(TITLE + " update")
					.setMessage("To update itself, the game needs permission to install apps. Allow it on the next "
							+ "screen, then come back; the update will be offered again.")
					.setPositiveButton("Open settings", (d, w) -> a.startActivity(new Intent(
							Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + a.getPackageName()))))
					.setNegativeButton("Later", null).show();
			apk.delete();
			return;
		}
		// The session API needs no FileProvider; Android shows its own confirmation
		try {
			PackageInstaller pi = a.getPackageManager().getPackageInstaller();
			PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(
					PackageInstaller.SessionParams.MODE_FULL_INSTALL);
			params.setAppPackageName(a.getPackageName());
			int id = pi.createSession(params);
			try (PackageInstaller.Session session = pi.openSession(id)) {
				try (InputStream in = new FileInputStream(apk);
						OutputStream os = session.openWrite("update.apk", 0, apk.length())) {
					copy(in, os);
					session.fsync(os);
				}
				String action = a.getPackageName() + ".UPDATE_STATUS";
				BroadcastReceiver receiver = new BroadcastReceiver() {
					@Override
					public void onReceive(Context ctx, Intent intent) {
						int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, -999);
						if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
							Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
							if (confirm != null)
								a.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
							return;
						}
						a.unregisterReceiver(this);
						apk.delete();
						if (status != PackageInstaller.STATUS_SUCCESS) {
							String msg = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
							new AlertDialog.Builder(a).setTitle(TITLE + " update")
									.setMessage("Android did not install the update"
											+ (msg != null ? " (" + msg + ")" : "") + ". The game stays as it is.")
									.setPositiveButton("OK", null).show();
						}
					}
				};
				if (Build.VERSION.SDK_INT >= 33)
					a.registerReceiver(receiver, new IntentFilter(action), Context.RECEIVER_NOT_EXPORTED);
				else
					a.registerReceiver(receiver, new IntentFilter(action));
				int flags = Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0;
				PendingIntent pending = PendingIntent.getBroadcast(a, id,
						new Intent(action).setPackage(a.getPackageName()), flags | PendingIntent.FLAG_UPDATE_CURRENT);
				session.commit(pending.getIntentSender());
			}
		} catch (Exception e) {
			apk.delete();
			new AlertDialog.Builder(a).setTitle(TITLE + " update")
					.setMessage("The update could not be handed to Android's installer.")
					.setPositiveButton("OK", null).show();
		}
	}

	static String sha256(File f) throws Exception {
		MessageDigest md = MessageDigest.getInstance("SHA-256");
		try (InputStream in = new FileInputStream(f)) {
			byte[] buf = new byte[65536];
			int n;
			while ((n = in.read(buf)) > 0)
				md.update(buf, 0, n);
		}
		StringBuilder sb = new StringBuilder();
		for (byte b : md.digest())
			sb.append(String.format("%02x", b));
		return sb.toString();
	}

	// ---- settings ----

	public static void showSettings(final Activity a) {
		final SharedPreferences prefs = a.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
		LinearLayout box = new LinearLayout(a);
		box.setOrientation(LinearLayout.VERTICAL);
		box.setPadding(48, 16, 48, 0);
		TextView help = new TextView(a);
		help.setText("You're on version " + gameVersion(a) + ". To update, open a downloaded picori-seal zip "
				+ "with this game, or let it look by itself: pick a folder where you save new versions, or "
				+ "give a private source (a latest.json URL, or github:you/private-repo with a read-only token). "
				+ "Builds hold your ROM, so keep them private.");
		final EditText source = new EditText(a);
		source.setHint("https://.../latest.json or github:you/repo");
		source.setText(prefs.getString("source", ""));
		source.setSingleLine();
		final EditText token = new EditText(a);
		token.setHint("token (only for a private GitHub repo)");
		token.setText(prefs.getString("token", ""));
		token.setSingleLine();
		token.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
		box.addView(help);
		box.addView(source);
		box.addView(token);
		new AlertDialog.Builder(a).setTitle("Update settings").setView(box)
				.setPositiveButton("Save", (d, w) -> {
					save(prefs, source, token);
					check(a);
				})
				.setNeutralButton("Pick a folder", (d, w) -> {
					save(prefs, source, token);
					a.startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_FOLDER);
				})
				.setNegativeButton("Cancel", null).show();
	}

	private static void save(SharedPreferences prefs, EditText source, EditText token) {
		prefs.edit().putString("source", source.getText().toString().trim())
				.putString("token", token.getText().toString().trim()).putBoolean("check", true).apply();
	}
}
