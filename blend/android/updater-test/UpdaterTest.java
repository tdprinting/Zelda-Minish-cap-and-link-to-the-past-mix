package com.picoriseal.game;

import android.content.*;
import java.io.*;
import java.util.*;

public class UpdaterTest {
	static class Prefs implements SharedPreferences, SharedPreferences.Editor {
		Map<String, Object> m = new HashMap<>();
		public Map<String, ?> getAll() { return m; }
		public String getString(String k, String d) { return m.containsKey(k) ? (String) m.get(k) : d; }
		public Set<String> getStringSet(String k, Set<String> d) { return d; }
		public int getInt(String k, int d) { return d; }
		public long getLong(String k, long d) { return d; }
		public float getFloat(String k, float d) { return d; }
		public boolean getBoolean(String k, boolean d) { return m.containsKey(k) ? (Boolean) m.get(k) : d; }
		public boolean contains(String k) { return m.containsKey(k); }
		public Editor edit() { return this; }
		public void registerOnSharedPreferenceChangeListener(OnSharedPreferenceChangeListener l) {}
		public void unregisterOnSharedPreferenceChangeListener(OnSharedPreferenceChangeListener l) {}
		public Editor putString(String k, String v) { m.put(k, v); return this; }
		public Editor putStringSet(String k, Set<String> v) { return this; }
		public Editor putInt(String k, int v) { return this; }
		public Editor putLong(String k, long v) { return this; }
		public Editor putFloat(String k, float v) { return this; }
		public Editor putBoolean(String k, boolean v) { m.put(k, v); return this; }
		public Editor remove(String k) { m.remove(k); return this; }
		public Editor clear() { m.clear(); return this; }
		public boolean commit() { return true; }
		public void apply() {}
	}
	static class Ctx extends ContextWrapper {
		Prefs prefs = new Prefs(); File dir;
		Ctx(File dir) { super(null); this.dir = dir; }
		@Override public SharedPreferences getSharedPreferences(String n, int mode) { return prefs; }
		@Override public File getExternalFilesDir(String t) { return dir; }
		@Override public android.content.pm.PackageManager getPackageManager() { throw new RuntimeException("no pm"); }
	}
	static void ok(boolean c, String what) { System.out.println((c ? "PASS " : "FAIL ") + what); if (!c) failed = true; }
	static boolean failed;

	public static void main(String[] a) throws Exception {
		File dir = new File(a[0]); dir.mkdirs();
		Ctx ctx = new Ctx(dir);
		ok(Updater.compare("1.2", "1.1") > 0 && Updater.compare("v1.10", "1.9") > 0 && Updater.compare("1.2.0", "1.2") == 0, "versions");
		ok(Updater.BUILD_NAME.matcher("picori-seal-v1.3 (1).zip").matches(), "browser-renamed zip name");
		// manifest with zip + apk (APK version 0 since there is no package manager here)
		Updater.Candidate c = Updater.checkManifest(ctx, "http://127.0.0.1:8765/latest.json", "");
		ok(c != null && "1.2".equals(c.version) && c.url.endsWith("/picori-seal-v1.2.zip") && c.sha256 != null, "manifest zip");
		c = Updater.checkManifest(ctx, "http://127.0.0.1:8765/withapk.json", "");
		ok(c != null && c.apk && c.url.endsWith("/picori-seal-v1.2.apk"), "manifest prefers same-version apk");
		System.setProperty("picori.github.api", "http://127.0.0.1:8766");
		c = Updater.checkGitHub(ctx, "tee/builds", "secret-token");
		ok(c != null && !c.apk && "1.2".equals(c.version) && c.url.endsWith("/releases/assets/2") && c.sha256 != null, "github release asset");
		File zip = new File(dir, "dl.zip");
		Updater.get(c.url, "secret-token", "application/octet-stream", zip, null);
		ok(c.sha256.equals(Updater.sha256(zip)), "github download + sha256 (token dropped on redirect)");
		new FileOutputStream(new File(dir, "player1.sav")).write("save".getBytes());
		String err = Updater.installGame(ctx, zip, c.version);
		ok(err == null && new File(dir, "zelda-blend.gba").length() == 16777216 && new File(dir, "previous/player1.sav").isFile()
				&& "1.2".equals(ctx.prefs.getString("gameVersion", null)), "install game from zip: " + err);
		ok(Updater.gameVersion(ctx).equals("1.2"), "game version now 1.2");
		ok(Updater.checkManifest(ctx, "http://127.0.0.1:8765/latest.json", "") == null, "1.2 no longer offered");
		// damaged zip
		File bad = new File(dir, "bad.zip");
		byte[] all = java.nio.file.Files.readAllBytes(zip.toPath());
		all[3000000] ^= 0x55; all[3000001] ^= 0x55;
		java.nio.file.Files.write(bad.toPath(), all);
		long before = new File(dir, "zelda-blend.gba").lastModified();
		try { err = Updater.installGame(ctx, bad, "1.3"); } catch (Exception e) { err = "exception " + e; }
		ok(err != null && "1.2".equals(ctx.prefs.getString("gameVersion", null)), "damaged zip refused: " + err);
		System.exit(failed ? 1 : 0);
	}
}
