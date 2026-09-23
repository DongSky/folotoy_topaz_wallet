package com.folotoy.passport.wallet.core;

import static org.junit.Assert.*;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import java.util.zip.CRC32;
import org.junit.Test;

public class SnapshotBackupTest {
    @Test public void pcw1EncodingHasExactHeaderRecordAndCrc() {
        byte[] pixels = new byte[Pcw1.PAGE_PIXEL_BYTES];
        Arrays.fill(pixels, (byte) 0x12);
        int[] palette = new int[16];
        Arrays.fill(palette, 0xff112233);
        DevicePage page = new DevicePage(DevicePage.KIND_CARD, false, palette, pixels);
        byte[] encoded = Pcw1.encode(java.util.List.of(page), 1_795_000_000L);
        assertEquals(Pcw1.HEADER_BYTES + Pcw1.PAGE_RECORD_BYTES, encoded.length);
        assertArrayEquals(new byte[]{'P','C','W','1'}, Arrays.copyOf(encoded, 4));
        ByteBuffer h = ByteBuffer.wrap(encoded).order(ByteOrder.LITTLE_ENDIAN);
        assertEquals(1, h.getShort(4)); assertEquals(1, h.getShort(6));
        assertEquals(216, h.getShort(8)); assertEquals(256, h.getShort(10));
        assertEquals(encoded.length, h.getInt(12)); assertEquals(1_795_000_000L, h.getLong(16));
        CRC32 crc = new CRC32(); crc.update(encoded, 32, encoded.length - 32);
        assertEquals((int) crc.getValue(), h.getInt(24));
        assertEquals(0x33, encoded[32 + 4]); assertEquals(0x22, encoded[32 + 5]);
        assertEquals(0x11, encoded[32 + 6]); assertEquals(0xff, encoded[32 + 7] & 0xff);
    }

    @Test(expected = IllegalArgumentException.class)
    public void assetsMustBePrivate() {
        new DevicePage(DevicePage.KIND_ASSET, false, new int[16], new byte[Pcw1.PAGE_PIXEL_BYTES]);
    }

    @Test(expected = IllegalArgumentException.class)
    public void cardMustNeverBePrivate() { new DevicePage(DevicePage.KIND_CARD,true,opaquePalette(),new byte[Pcw1.PAGE_PIXEL_BYTES]); }

    @Test(expected = IllegalArgumentException.class)
    public void snapshotNeverTruncatesOver52Pages() {
        DevicePage p = new DevicePage(DevicePage.KIND_CARD, false, opaquePalette(), new byte[Pcw1.PAGE_PIXEL_BYTES]);
        Pcw1.encode(java.util.Collections.nCopies(53, p), 1);
    }

    @Test public void largestSnapshotFitsScreenshotReservedBank() {
        java.util.ArrayList<DevicePage> pages=new java.util.ArrayList<>();
        pages.add(new DevicePage(DevicePage.KIND_CARD,false,opaquePalette(),new byte[Pcw1.PAGE_PIXEL_BYTES]));
        DevicePage social=new DevicePage(DevicePage.KIND_SOCIAL,false,opaquePalette(),new byte[Pcw1.PAGE_PIXEL_BYTES]);
        for(int i=1;i<52;i++)pages.add(social);
        byte[] packageBytes=Pcw1.encode(pages,1);
        assertEquals(52,Pcw1.MAX_PAGES);
        assertEquals(32+52*27716,packageBytes.length);
        // PCW commit marker is the final 32 bytes of each bank, not a 4KB metadata page.
        assertTrue(packageBytes.length<0x160000-32);
        pages.add(social);
        assertThrows(IllegalArgumentException.class,()->Pcw1.encode(pages,1));
    }

    @Test public void backupVersionAndLimitsRoundTrip() {
        WalletBackup backup = new WalletBackup(1,new Profile("Sample","Offline card",""),java.util.List.of(new SocialLink("Web","sample","https://example.com")),java.util.List.of(new PaymentCode("Alipay","https://qr.alipay.com/fkx123456789")),java.util.List.of(new WalletEntry(Network.BITCOIN,"1BoatSLRHtKNngkdXEeobR76b53LETtpyT","BTC","Sample BTC")),"CNY");
        String json = BackupCodec.encode(backup);
        WalletBackup restored = BackupCodec.decode(json);
        assertEquals(WalletBackup.SCHEMA_VERSION, restored.version());
        assertEquals(backup.profile(), restored.profile());
        assertEquals(backup.entries(), restored.entries());
        assertFalse(json.contains("rpcUrl"));
        assertFalse(json.toLowerCase().contains("privatekey"));
    }

    @Test(expected = IllegalArgumentException.class)
    public void unsupportedBackupVersionRejected() { BackupCodec.decode("{\"version\":99}"); }

    @Test(expected = IllegalArgumentException.class)
    public void trailingBackupJunkRejected() { BackupCodec.decode("{\"version\":1} junk"); }

    @Test(expected = IllegalArgumentException.class)
    public void duplicateBackupFieldRejected() { BackupCodec.decode("{\"version\":1,\"version\":1}"); }

    @Test(expected = IllegalArgumentException.class)
    public void coercibleBackupScalarTypeRejected() {
        String valid=BackupCodec.encode(new WalletBackup(1,new Profile("N","B",""),java.util.List.of(),java.util.List.of(),java.util.List.of()));
        BackupCodec.decode(valid.replace("\"nickname\": \"N\"","\"nickname\": 7"));
    }

    @Test(expected = IllegalArgumentException.class)
    public void deeplyNestedBackupRejectedBeforeObjectParsing() {
        BackupCodec.decode("[".repeat(18)+"0"+"]".repeat(18));
    }

    private static int[] opaquePalette() { int[] p = new int[16]; Arrays.fill(p, 0xff000000); return p; }
}
