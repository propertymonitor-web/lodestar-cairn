// Engineered by: Aries Thorne & Todd Steinhauer for Wolf Reactive Systems (WRS)
package com.wolfreactive.cairn

/**
 * The text link that CAIRN firmware 1.2.0 and newer carries inside MeshCore's Bluetooth companion connection
 * (see cairn/CairnOut.h in the firmware). One extra frame type, 0xC8, in both directions:
 *   tablet -> radio: [0xC8][one line of text]
 *   radio -> tablet: [0xC8][a piece of the radio's text output]; the pieces are joined and split at line ends.
 * Kept free of Android classes so it can be unit-tested on any computer.
 */
object Tunnel {
    const val CODE = 0xC8
    /** A MeshCore frame holds at most 176 bytes; one is the type byte. */
    const val MAX_LINE_BYTES = 172
    const val MAX_LINE_IN = 16000

    /** One line for the radio as a single Bluetooth frame, or null when it is empty or does not fit. */
    fun lineFrame(line: String): ByteArray? {
        val body = line.trimEnd('\r', '\n').toByteArray(Charsets.UTF_8)
        if (body.isEmpty() || body.size > MAX_LINE_BYTES) return null
        val f = ByteArray(body.size + 1)
        f[0] = CODE.toByte()
        System.arraycopy(body, 0, f, 1, body.size)
        return f
    }

    /** The bytes of text carried by a frame from the radio, or null for any other kind of frame. */
    fun textBytes(frame: ByteArray): ByteArray? {
        if (frame.size < 2 || (frame[0].toInt() and 0xFF) != CODE) return null
        return frame.copyOfRange(1, frame.size)
    }
}

/** Joins pieces of a byte stream and hands back whole lines. A multi-byte character split between pieces stays whole. */
class LineSplitter(private val maxLine: Int = Tunnel.MAX_LINE_IN) {
    private var buf = ByteArray(256)
    private var n = 0
    private var skipping = false

    @Synchronized
    fun feed(data: ByteArray, off: Int = 0, len: Int = data.size - off): List<String> {
        val out = ArrayList<String>()
        for (i in off until off + len) {
            val b = data[i]
            if (b == '\n'.code.toByte()) {
                if (!skipping && n > 0) out.add(String(buf, 0, n, Charsets.UTF_8).trimEnd('\r'))
                n = 0; skipping = false
                continue
            }
            if (skipping) continue
            if (n >= maxLine) { n = 0; skipping = true; continue }   // a runaway line: drop it, resume at the next line end
            if (n == buf.size) buf = buf.copyOf(minOf(buf.size * 2, maxLine))
            buf[n++] = b
        }
        return out
    }

    @Synchronized
    fun clear() { n = 0; skipping = false }
}
