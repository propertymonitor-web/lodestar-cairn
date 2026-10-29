package com.wolfreactive.cairn

import org.junit.Assert.*
import org.junit.Test

class TunnelTest {
    @Test fun lineBecomesOneFrameWithTheTypeByte() {
        val f = Tunnel.lineFrame("nova hello\n")!!
        assertEquals(0xC8, f[0].toInt() and 0xFF)
        assertEquals("nova hello", String(f, 1, f.size - 1))
    }

    @Test fun emptyAndOversizeLinesAreRefused() {
        assertNull(Tunnel.lineFrame(""))
        assertNull(Tunnel.lineFrame("\r\n"))
        assertNotNull(Tunnel.lineFrame("x".repeat(Tunnel.MAX_LINE_BYTES)))
        assertNull(Tunnel.lineFrame("x".repeat(Tunnel.MAX_LINE_BYTES + 1)))
        // the limit counts bytes, not characters
        assertNull(Tunnel.lineFrame("é".repeat(Tunnel.MAX_LINE_BYTES / 2 + 1)))
    }

    @Test fun framesFromTheRadioKeepOnlyOurType() {
        assertNull(Tunnel.textBytes(byteArrayOf(0x83.toByte(), 1, 2)))
        assertNull(Tunnel.textBytes(byteArrayOf(0xC8.toByte())))
        assertEquals("hi", String(Tunnel.textBytes(byteArrayOf(0xC8.toByte(), 'h'.code.toByte(), 'i'.code.toByte()))!!))
    }

    @Test fun linesAreRebuiltFromPieces() {
        val s = LineSplitter()
        assertTrue(s.feed("{\"cairn\":1,\"id".toByteArray()).isEmpty())
        val got = s.feed("\":\"LC-K7M2P4XQ\"}\r\n# ok\n".toByteArray())
        assertEquals(listOf("{\"cairn\":1,\"id\":\"LC-K7M2P4XQ\"}", "# ok"), got)
    }

    @Test fun aCharacterSplitBetweenPiecesStaysWhole() {
        val s = LineSplitter()
        val bytes = "café ☃\n".toByteArray(Charsets.UTF_8)
        val out = ArrayList<String>()
        for (b in bytes) out.addAll(s.feed(byteArrayOf(b)))   // one byte at a time, the worst case
        assertEquals(listOf("café ☃"), out)
    }

    @Test fun blankLinesAreSkippedAndRunawayLinesAreDropped() {
        val s = LineSplitter(maxLine = 10)
        assertEquals(listOf("a", "b"), s.feed("a\n\n\nb\n".toByteArray()))
        assertEquals(listOf("ok"), s.feed(("x".repeat(50) + "\nok\n").toByteArray()))
    }
}
