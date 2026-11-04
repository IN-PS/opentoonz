package org.opentoonz.mobile;

import android.content.Context;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.media.MediaPlayer;
import android.net.Uri;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.DataInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;
import java.util.Properties;

/**
 * Sound track of a project (lip-sync / timing reference), stored inside the
 * project folder as 16-bit PCM {@code audio.wav} + {@code audio.properties}
 * (start frame, volume).  Decoding, playback and AAC encoding use the Android
 * media framework (desktop OpenToonz uses Qt Multimedia / ffmpeg instead).
 */
final class ProjectAudio {
    static final String WAV = "audio.wav", PROPS = "audio.properties";

    final File dir;
    int offsetFrames;      // animation frame where the sound starts
    float volume = 1f;
    // WAV info
    int sampleRate, channels;
    long dataOffset, dataBytes;

    private MediaPlayer mPlayer;

    private ProjectAudio(File dir) { this.dir = dir; }

    File wavFile() { return new File(dir, WAV); }

    /** Loads the project's sound track, or null if it has none. */
    static ProjectAudio load(File projectDir) {
        ProjectAudio a = new ProjectAudio(projectDir);
        if (!a.wavFile().exists()) return null;
        Properties p = new Properties();
        try (InputStream in = new FileInputStream(new File(projectDir, PROPS))) {
            p.load(in);
        } catch (IOException ignored) {
        }
        a.offsetFrames = Integer.parseInt(p.getProperty("offsetFrames", "0"));
        a.volume = Float.parseFloat(p.getProperty("volume", "1"));
        try {
            a.readHeader();
        } catch (IOException e) {
            return null;
        }
        return a;
    }

    void saveProps() throws IOException {
        Properties p = new Properties();
        p.setProperty("offsetFrames", String.valueOf(offsetFrames));
        p.setProperty("volume", String.valueOf(volume));
        try (OutputStream out = new FileOutputStream(new File(dir, PROPS))) {
            p.store(out, "OpenToonz Mobile sound track");
        }
    }

    static void remove(File projectDir) {
        new File(projectDir, WAV).delete();
        new File(projectDir, PROPS).delete();
    }

    double durationSec() {
        return sampleRate > 0 ? dataBytes / (2.0 * channels * sampleRate) : 0;
    }

    // ------------------------------------------------------------ WAV I/O
    static void writeWavHeader(RandomAccessFile f, int rate, int ch, long dataLen) throws IOException {
        ByteBuffer b = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN);
        b.put(new byte[]{'R', 'I', 'F', 'F'}).putInt((int) (36 + dataLen))
                .put(new byte[]{'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}).putInt(16)
                .putShort((short) 1).putShort((short) ch).putInt(rate).putInt(rate * ch * 2)
                .putShort((short) (ch * 2)).putShort((short) 16)
                .put(new byte[]{'d', 'a', 't', 'a'}).putInt((int) dataLen);
        f.seek(0);
        f.write(b.array());
    }

    /** Writes interleaved 16-bit PCM as a WAV file (used by import and tests). */
    static void writeWav(File out, short[] pcm, int rate, int ch) throws IOException {
        try (RandomAccessFile f = new RandomAccessFile(out, "rw")) {
            f.setLength(0);
            writeWavHeader(f, rate, ch, pcm.length * 2L);
            ByteBuffer b = ByteBuffer.allocate(pcm.length * 2).order(ByteOrder.LITTLE_ENDIAN);
            for (short v : pcm) b.putShort(v);
            f.write(b.array());
        }
    }

    void readHeader() throws IOException {
        try (DataInputStream in = new DataInputStream(new BufferedInputStream(new FileInputStream(wavFile())))) {
            byte[] h = new byte[12];
            in.readFully(h);
            if (h[0] != 'R' || h[8] != 'W') throw new IOException("not a WAV file");
            long pos = 12;
            while (true) {
                byte[] id = new byte[4];
                in.readFully(id);
                int len = Integer.reverseBytes(in.readInt());
                pos += 8;
                String tag = new String(id, "ASCII");
                if (tag.equals("fmt ")) {
                    byte[] f = new byte[len];
                    in.readFully(f);
                    ByteBuffer b = ByteBuffer.wrap(f).order(ByteOrder.LITTLE_ENDIAN);
                    b.getShort();
                    channels = b.getShort();
                    sampleRate = b.getInt();
                } else if (tag.equals("data")) {
                    dataOffset = pos;
                    dataBytes = Math.min(len & 0xFFFFFFFFL, wavFile().length() - pos);
                    return;
                } else {
                    in.skipBytes(len);
                }
                pos += len;
            }
        }
    }

    /** Peak amplitude (0..1) of every animation frame of the sound. */
    float[] peaks(int fps) throws IOException {
        int frames = (int) Math.ceil(durationSec() * fps);
        float[] peaks = new float[Math.max(0, frames)];
        long bytesPerFrame = Math.max(2L * channels, (long) sampleRate * channels * 2 / fps);
        byte[] buf = new byte[(int) Math.min(1 << 20, bytesPerFrame * 64)];
        try (InputStream in = new BufferedInputStream(new FileInputStream(wavFile()))) {
            long skip = dataOffset;
            while (skip > 0) skip -= in.skip(skip);
            long done = 0;
            int n;
            while (done < dataBytes && (n = in.read(buf, 0, (int) Math.min(buf.length, dataBytes - done))) > 0) {
                for (int i = 0; i + 1 < n; i += 2) {
                    int frame = (int) ((done + i) / bytesPerFrame);
                    if (frame >= peaks.length) break;
                    int v = Math.abs((short) ((buf[i] & 0xFF) | (buf[i + 1] << 8)));
                    float f = v / 32768f;
                    if (f > peaks[frame]) peaks[frame] = f;
                }
                done += n;
            }
        }
        return peaks;
    }

    // ------------------------------------------------------------ import
    /** Decodes any audio file Android can read into the project's WAV. */
    static String importFrom(Context ctx, Uri uri, File projectDir) {
        MediaExtractor ex = new MediaExtractor();
        MediaCodec dec = null;
        File tmp = new File(projectDir, WAV + ".tmp");
        try {
            ex.setDataSource(ctx, uri, null);
            int track = -1;
            for (int i = 0; i < ex.getTrackCount(); i++) {
                String mime = ex.getTrackFormat(i).getString(MediaFormat.KEY_MIME);
                if (mime != null && mime.startsWith("audio/")) { track = i; break; }
            }
            if (track < 0) return "no audio track";
            ex.selectTrack(track);
            MediaFormat fmt = ex.getTrackFormat(track);
            dec = MediaCodec.createDecoderByType(fmt.getString(MediaFormat.KEY_MIME));
            dec.configure(fmt, null, null, 0);
            dec.start();
            int rate = fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE);
            int ch = fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
            long total = 0;
            try (RandomAccessFile out = new RandomAccessFile(tmp, "rw")) {
                out.setLength(0);
                writeWavHeader(out, rate, ch, 0);
                MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
                boolean inDone = false, outDone = false;
                byte[] chunk = new byte[0];
                while (!outDone) {
                    if (!inDone) {
                        int in = dec.dequeueInputBuffer(10_000);
                        if (in >= 0) {
                            ByteBuffer b = dec.getInputBuffer(in);
                            int n = ex.readSampleData(b, 0);
                            if (n < 0) {
                                dec.queueInputBuffer(in, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                                inDone = true;
                            } else {
                                dec.queueInputBuffer(in, 0, n, ex.getSampleTime(), 0);
                                ex.advance();
                            }
                        }
                    }
                    int o = dec.dequeueOutputBuffer(info, 10_000);
                    if (o == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                        MediaFormat of = dec.getOutputFormat();
                        rate = of.getInteger(MediaFormat.KEY_SAMPLE_RATE);
                        ch = of.getInteger(MediaFormat.KEY_CHANNEL_COUNT);
                    } else if (o >= 0) {
                        ByteBuffer b = dec.getOutputBuffer(o);
                        if (b != null && info.size > 0) {
                            if (chunk.length < info.size) chunk = new byte[info.size];
                            b.position(info.offset);
                            b.get(chunk, 0, info.size);
                            out.write(chunk, 0, info.size);  // 16-bit LE PCM (decoder default)
                            total += info.size;
                        }
                        dec.releaseOutputBuffer(o, false);
                        if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) outDone = true;
                    }
                }
                writeWavHeader(out, rate, ch, total);
            }
            if (total == 0) return "decoder produced no samples";
            File wav = new File(projectDir, WAV);
            wav.delete();
            if (!tmp.renameTo(wav)) return "cannot store the audio file";
            ProjectAudio a = new ProjectAudio(projectDir);
            a.saveProps();
            return null;
        } catch (IOException | RuntimeException e) {
            return e.toString();
        } finally {
            tmp.delete();
            try { if (dec != null) { dec.stop(); dec.release(); } } catch (RuntimeException ignored) {}
            ex.release();
        }
    }

    // ------------------------------------------------------------ playback
    private MediaPlayer player() throws IOException {
        if (mPlayer == null) {
            mPlayer = new MediaPlayer();
            mPlayer.setDataSource(wavFile().getAbsolutePath());
            mPlayer.prepare();
            mPlayer.setVolume(volume, volume);
        }
        return mPlayer;
    }

    /** Starts playing in sync with animation frame {@code row}. */
    void play(int row, int fps) {
        try {
            int ms = (int) Math.round((row - offsetFrames) * 1000.0 / fps);
            if (ms < 0 || ms >= durationSec() * 1000) return;
            MediaPlayer p = player();
            p.seekTo(ms);
            p.start();
        } catch (IOException | RuntimeException ignored) {
        }
    }

    /** Plays one frame worth of sound at {@code row} (lip-sync scrubbing). */
    void scrub(int row, int fps, android.os.Handler h) {
        play(row, fps);
        h.postDelayed(new Runnable() {
            @Override public void run() { pause(); }
        }, Math.max(60, 1000 / fps));
    }

    void pause() {
        try { if (mPlayer != null && mPlayer.isPlaying()) mPlayer.pause(); } catch (RuntimeException ignored) {}
    }

    void release() {
        if (mPlayer != null) { mPlayer.release(); mPlayer = null; }
    }

    // ------------------------------------------------------------ AAC for MP4
    static final class EncodedAudio {
        MediaFormat format;
        final List<ByteBuffer> samples = new ArrayList<>();
        final List<MediaCodec.BufferInfo> infos = new ArrayList<>();
    }

    /** Encodes the part of the sound that falls inside the animation to AAC. */
    EncodedAudio encodeAac(int frames, int fps) throws IOException {
        long startByte = 0, startUs = 0;
        if (offsetFrames >= 0) startUs = offsetFrames * 1_000_000L / fps;
        else startByte = (long) (-offsetFrames) * sampleRate / fps * channels * 2;
        long maxUs = frames * 1_000_000L / fps - startUs;
        long maxBytes = Math.min(dataBytes - startByte, maxUs * sampleRate / 1_000_000L * channels * 2);
        if (maxBytes <= 0) return null;
        MediaFormat fmt = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, sampleRate, channels);
        fmt.setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC);
        fmt.setInteger(MediaFormat.KEY_BIT_RATE, 128_000);
        MediaCodec enc = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC);
        EncodedAudio res = new EncodedAudio();
        try (InputStream in = new BufferedInputStream(new FileInputStream(wavFile()))) {
            long skip = dataOffset + startByte;
            while (skip > 0) skip -= in.skip(skip);
            enc.configure(fmt, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            enc.start();
            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            long fed = 0;
            boolean inDone = false, outDone = false;
            byte[] buf = new byte[8192];
            while (!outDone) {
                if (!inDone) {
                    int i = enc.dequeueInputBuffer(10_000);
                    if (i >= 0) {
                        ByteBuffer b = enc.getInputBuffer(i);
                        int want = (int) Math.min(Math.min(b.remaining(), buf.length), maxBytes - fed);
                        int n = want > 0 ? in.read(buf, 0, want) : -1;
                        long pts = startUs + fed * 1_000_000L / (2L * channels * sampleRate);
                        if (n <= 0) {
                            enc.queueInputBuffer(i, 0, 0, pts, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            inDone = true;
                        } else {
                            // apply the track volume
                            for (int k = 0; k + 1 < n; k += 2) {
                                int v = (short) ((buf[k] & 0xFF) | (buf[k + 1] << 8));
                                v = Math.max(-32768, Math.min(32767, Math.round(v * volume)));
                                buf[k] = (byte) v;
                                buf[k + 1] = (byte) (v >> 8);
                            }
                            b.put(buf, 0, n);
                            enc.queueInputBuffer(i, 0, n, pts, 0);
                            fed += n;
                        }
                    }
                }
                int o = enc.dequeueOutputBuffer(info, 10_000);
                if (o == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    res.format = enc.getOutputFormat();
                } else if (o >= 0) {
                    ByteBuffer b = enc.getOutputBuffer(o);
                    if (b != null && info.size > 0 && (info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0) {
                        ByteBuffer copy = ByteBuffer.allocateDirect(info.size);
                        b.position(info.offset);
                        b.limit(info.offset + info.size);
                        copy.put(b);
                        copy.flip();
                        MediaCodec.BufferInfo ci = new MediaCodec.BufferInfo();
                        ci.set(0, info.size, info.presentationTimeUs, info.flags);
                        res.samples.add(copy);
                        res.infos.add(ci);
                    }
                    enc.releaseOutputBuffer(o, false);
                    if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) outDone = true;
                }
            }
        } finally {
            try { enc.stop(); } catch (RuntimeException ignored) {}
            enc.release();
        }
        return res.format != null && !res.samples.isEmpty() ? res : null;
    }
}
