package org.opentoonz.mobile;

import android.media.Image;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.MediaMuxer;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * H.264 / MP4 export through Android's MediaCodec + MediaMuxer.  Desktop
 * OpenToonz delegates MP4 to an external ffmpeg binary, which is not
 * available on Android, so the platform hardware encoder is used instead.
 * Frames are rendered by the native engine directly into the encoder's
 * YUV420 input planes.
 */
final class Mp4Exporter {
    interface Progress { void onFrame(int done, int total); }

    private Mp4Exporter() {}

    /** @return null on success, otherwise an error message. */
    static String export(File out, int maxSide, Progress progress) {
        int w = NativeEngine.getWidth(), h = NativeEngine.getHeight();
        float s = Math.min(1f, maxSide / (float) Math.max(w, h));
        // H.264 encoders want even sizes; multiples of 16 are the safest.
        int ew = Math.max(16, (Math.round(w * s) / 16) * 16);
        int eh = Math.max(16, (Math.round(h * s) / 16) * 16);
        int fps = NativeEngine.getFps();
        int frames = NativeEngine.getFrameCount();

        MediaCodec codec = null;
        MediaMuxer muxer = null;
        try {
            MediaFormat fmt = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, ew, eh);
            fmt.setInteger(MediaFormat.KEY_COLOR_FORMAT,
                    MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible);
            fmt.setInteger(MediaFormat.KEY_BIT_RATE, Math.max(1_000_000, ew * eh * fps / 4));
            fmt.setInteger(MediaFormat.KEY_FRAME_RATE, fps);
            fmt.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1);
            codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC);
            codec.configure(fmt, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            codec.start();
            muxer = new MediaMuxer(out.getAbsolutePath(), MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);

            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            int track = -1;
            boolean muxing = false, inputDone = false, outputDone = false;
            int frame = 0, written = 0;
            long frameUs = 1_000_000L / fps;
            while (!outputDone) {
                if (!inputDone) {
                    int in = codec.dequeueInputBuffer(10_000);
                    if (in >= 0) {
                        if (frame >= frames) {
                            codec.queueInputBuffer(in, 0, 0, frame * frameUs,
                                    MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            inputDone = true;
                        } else {
                            Image img = codec.getInputImage(in);
                            Image.Plane[] pl = img.getPlanes();
                            NativeEngine.renderYuv(frame, ew, eh,
                                    pl[0].getBuffer(), pl[0].getRowStride(), pl[0].getPixelStride(),
                                    pl[1].getBuffer(), pl[1].getRowStride(), pl[1].getPixelStride(),
                                    pl[2].getBuffer(), pl[2].getRowStride(), pl[2].getPixelStride());
                            codec.queueInputBuffer(in, 0, ew * eh * 3 / 2, frame * frameUs, 0);
                            frame++;
                            if (progress != null) progress.onFrame(frame, frames);
                        }
                    }
                }
                int outIdx = codec.dequeueOutputBuffer(info, 10_000);
                if (outIdx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    track = muxer.addTrack(codec.getOutputFormat());
                    muxer.start();
                    muxing = true;
                } else if (outIdx >= 0) {
                    ByteBuffer buf = codec.getOutputBuffer(outIdx);
                    if ((info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) info.size = 0;
                    if (info.size > 0 && muxing && buf != null) {
                        buf.position(info.offset);
                        buf.limit(info.offset + info.size);
                        muxer.writeSampleData(track, buf, info);
                        written++;
                    }
                    codec.releaseOutputBuffer(outIdx, false);
                    if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) outputDone = true;
                }
            }
            if (written == 0) return "encoder produced no frames";
            return null;
        } catch (IOException | RuntimeException e) {
            return e.toString();
        } finally {
            try { if (codec != null) { codec.stop(); codec.release(); } } catch (RuntimeException ignored) {}
            try { if (muxer != null) { muxer.stop(); muxer.release(); } } catch (RuntimeException ignored) {}
        }
    }
}
