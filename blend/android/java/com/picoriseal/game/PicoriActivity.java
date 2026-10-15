package com.picoriseal.game;

import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.view.InputDevice;

import org.libsdl.app.SDLActivity;

/**
 * Starts the co-op host (blend/host/coop.c, built as libmain.so). Before the game
 * starts it tells the host which gamepads are the handheld's own built-in controls,
 * so those always play as player 1 and a Bluetooth or USB pad becomes player 2.
 * It also runs the auto updater (Updater.java).
 */
public class PicoriActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Updater.prepare(this); // before the game starts: drops an updated game file older than this APK
        super.onCreate(savedInstanceState);
        if (mBrokenLibraries) {
            return;
        }
        Updater.handleIntent(this, getIntent());
        Updater.check(this);
        nativeSetenv("PICORI_BUILTIN_PADS", builtInPads());
        try {
            long installed = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime / 1000;
            nativeSetenv("PICORI_APK_TIME", Long.toString(installed));
        } catch (Exception e) {
            // No install time: any updated game file in the files folder is used
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        Updater.handleIntent(this, intent);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (!Updater.onActivityResult(this, requestCode, resultCode, data)) {
            super.onActivityResult(requestCode, resultCode, data);
        }
    }

    private static String builtInPads() {
        StringBuilder names = new StringBuilder();
        if (Build.VERSION.SDK_INT < 29) {
            return ""; // isExternal() needs Android 10; the host falls back to the device name
        }
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice dev = InputDevice.getDevice(id);
            if (dev == null || dev.isVirtual() || dev.isExternal()) {
                continue;
            }
            int sources = dev.getSources();
            if ((sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                    || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
                names.append(dev.getName()).append('\n');
            }
        }
        return names.toString();
    }
}
