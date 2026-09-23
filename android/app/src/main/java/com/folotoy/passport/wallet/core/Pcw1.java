package com.folotoy.passport.wallet.core;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.List;
import java.util.zip.CRC32;

public final class Pcw1 {
    public static final int WIDTH=216,HEIGHT=256,HEADER_BYTES=32,PAGE_PIXEL_BYTES=27648,PAGE_RECORD_BYTES=27716,MAX_PAGES=52;
    private Pcw1() {}
    public static byte[] encode(List<DevicePage> pages,long createdUnixSeconds){
        if(pages==null||pages.isEmpty()||pages.size()>MAX_PAGES) throw new IllegalArgumentException("设备最多容纳 52 页；请减少资产、账号或缩短文字后重试（不会截断内容）");
        if(pages.get(0).kind()!=DevicePage.KIND_CARD) throw new IllegalArgumentException("First page must be card");
        int cards=0; for(DevicePage p:pages) if(p.kind()==DevicePage.KIND_CARD) cards++;
        if(cards!=1) throw new IllegalArgumentException("Snapshot must contain exactly one card page");
        int total=HEADER_BYTES+pages.size()*PAGE_RECORD_BYTES;
        ByteBuffer b=ByteBuffer.allocate(total).order(ByteOrder.LITTLE_ENDIAN);
        b.put(new byte[]{'P','C','W','1'}).putShort((short)1).putShort((short)pages.size()).putShort((short)WIDTH).putShort((short)HEIGHT)
                .putInt(total).putLong(createdUnixSeconds).putInt(0).putInt(0);
        for(DevicePage p:pages){
            b.put((byte)p.kind()).put((byte)(p.privatePage()?1:0)).putShort((short)0);
            for(int argb:p.palette()) b.put((byte)argb).put((byte)(argb>>>8)).put((byte)(argb>>>16)).put((byte)(argb>>>24));
            b.put(p.pixels());
        }
        CRC32 crc=new CRC32(); crc.update(b.array(),HEADER_BYTES,total-HEADER_BYTES); b.putInt(24,(int)crc.getValue()); return b.array();
    }
}
