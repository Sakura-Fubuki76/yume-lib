package io.github.sakurafubuki.yume.nativelib.soundtouch;

import java.io.Closeable;
import java.util.Objects;
import java.util.concurrent.atomic.AtomicBoolean;

public final class SoundTouch implements Closeable {
    public static final String SOUNDTOUCH_VERSION = "2.4.1";
    public static final long SOUNDTOUCH_VERSION_ID = 20401L;

    public static final int SETTING_USE_AA_FILTER = 0;
    public static final int SETTING_AA_FILTER_LENGTH = 1;
    public static final int SETTING_USE_QUICKSEEK = 2;
    public static final int SETTING_SEQUENCE_MS = 3;
    public static final int SETTING_SEEKWINDOW_MS = 4;
    public static final int SETTING_OVERLAP_MS = 5;
    public static final int SETTING_NOMINAL_INPUT_SEQUENCE = 6;
    public static final int SETTING_NOMINAL_OUTPUT_SEQUENCE = 7;
    public static final int SETTING_INITIAL_LATENCY = 8;

    private static final long UINT_MAX = 0xFFFF_FFFFL;
    private static final AtomicBoolean LIBRARIES_LOADED = new AtomicBoolean(false);

    private volatile long handle;
    private final AtomicBoolean disposed = new AtomicBoolean(false);

    static {
        loadLibraries();
    }

    public SoundTouch() {
        handle = createInstance();
    }

    public boolean isDisposed() {
        return disposed.get();
    }

    public void dispose() {
        if (disposed.compareAndSet(false, true)) {
            destroyInstance(handle);
            handle = 0L;
        }
    }

    @Override
    public void close() {
        dispose();
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            dispose();
        } finally {
            super.finalize();
        }
    }

    public static String getVersionString() {
        return SOUNDTOUCH_VERSION;
    }

    public static long getVersionId() {
        return SOUNDTOUCH_VERSION_ID;
    }

    public void setRate(float rate) {
        checkDisposed();
        setRate(handle, rate);
    }

    public void setTempo(float tempo) {
        checkDisposed();
        setTempo(handle, tempo);
    }

    public void setRateChange(float rate) {
        checkDisposed();
        setRateChange(handle, rate);
    }

    public void setTempoChange(float tempo) {
        checkDisposed();
        setTempoChange(handle, tempo);
    }

    public void setPitch(float pitch) {
        checkDisposed();
        setPitch(handle, pitch);
    }

    public void setPitchOctaves(float pitch) {
        checkDisposed();
        setPitchOctaves(handle, pitch);
    }

    public void setPitchSemiTones(float pitch) {
        checkDisposed();
        setPitchSemiTones(handle, pitch);
    }

    public void setChannels(long channels) {
        checkDisposed();
        checkUnsignedInt(channels);
        setChannels(handle, channels);
    }

    public void setSampleRate(long sampleRate) {
        checkDisposed();
        checkUnsignedInt(sampleRate);
        setSampleRate(handle, sampleRate);
    }

    public void flush() {
        checkDisposed();
        flush(handle);
    }

    public void putSamples(float[] samples, int offset, int frames) {
        checkDisposed();
        Objects.requireNonNull(samples, "samples cannot be null.");
        checkUnsignedInt(offset);
        checkUnsignedInt(frames);
        putSamples(handle, samples, offset, frames);
    }

    public void putSamples(short[] samples, int offset, int frames) {
        checkDisposed();
        Objects.requireNonNull(samples, "samples cannot be null.");
        checkUnsignedInt(offset);
        checkUnsignedInt(frames);
        putSamples_i16(handle, samples, offset, frames);
    }

    public void clear() {
        checkDisposed();
        clear(handle);
    }

    public boolean setSetting(int settingId, int value) {
        checkDisposed();
        return setSetting(handle, settingId, value) != 0;
    }

    public int getSetting(int settingId) {
        checkDisposed();
        return getSetting(handle, settingId);
    }

    public long numUncompressedSamples() {
        checkDisposed();
        return numUnprocessedSamples(handle);
    }

    public int receiveSamples(float[] outBuffer, int offset, int maxSamples) {
        checkDisposed();
        Objects.requireNonNull(outBuffer, "outBuffer cannot be null.");
        checkUnsignedInt(offset);
        checkUnsignedInt(maxSamples);
        return receiveSamples(handle, outBuffer, offset, maxSamples);
    }

    public int receiveSamplesI16(short[] outBuffer, int offset, int maxSamples) {
        checkDisposed();
        Objects.requireNonNull(outBuffer, "outBuffer cannot be null.");
        checkUnsignedInt(offset);
        checkUnsignedInt(maxSamples);
        return receiveSamples_i16(handle, outBuffer, offset, maxSamples);
    }

    public long numSamples() {
        checkDisposed();
        return numSamples(handle);
    }

    public boolean isEmpty() {
        checkDisposed();
        return isEmpty(handle) != 0;
    }

    private static void loadLibraries() {
        if (LIBRARIES_LOADED.compareAndSet(false, true)) {
            System.loadLibrary("SoundTouchDLL");
            System.loadLibrary("soundtouch");
        }
    }

    private void checkDisposed() {
        if (disposed.get()) {
            throw new IllegalStateException("The native instance has been disposed. Please create a new SoundTouch object for use.");
        }
    }

    private static void checkUnsignedInt(long value) {
        if (value < 0L) {
            throw new IllegalArgumentException("Unsigned int cannot be < 0.");
        }
        if (value > UINT_MAX) {
            throw new IllegalArgumentException("Unsigned int cannot be > " + UINT_MAX + ".");
        }
    }

    private static native long createInstance();
    private static native void destroyInstance(long handle);
    private static native void setRate(long handle, float rate);
    private static native void setTempo(long handle, float tempo);
    private static native void setRateChange(long handle, float rate);
    private static native void setTempoChange(long handle, float tempo);
    private static native void setPitch(long handle, float pitch);
    private static native void setPitchOctaves(long handle, float pitch);
    private static native void setPitchSemiTones(long handle, float pitch);
    private static native void setChannels(long handle, long channels);
    private static native void setSampleRate(long handle, long sampleRate);
    private static native void flush(long handle);
    private static native void putSamples(long handle, float[] samples, int offset, int frames);
    private static native void putSamples_i16(long handle, short[] samples, int offset, int frames);
    private static native void clear(long handle);
    private static native int setSetting(long handle, int settingId, int value);
    private static native int getSetting(long handle, int settingId);
    private static native long numUnprocessedSamples(long handle);
    private static native int receiveSamples(long handle, float[] outBuffer, int offset, int maxSamples);
    private static native int receiveSamples_i16(long handle, short[] outBuffer, int offset, int maxSamples);
    private static native long numSamples(long handle);
    private static native int isEmpty(long handle);
}
