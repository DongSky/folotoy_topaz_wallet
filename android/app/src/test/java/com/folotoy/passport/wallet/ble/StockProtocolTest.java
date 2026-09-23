package com.folotoy.passport.wallet.ble;

import org.junit.Test;
import static org.junit.Assert.*;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;

public class StockProtocolTest {
    static byte[] jpeg(int size){byte[] bytes=new byte[size];bytes[0]=(byte)0xff;bytes[1]=(byte)0xd8;bytes[size-2]=(byte)0xff;bytes[size-1]=(byte)0xd9;return bytes;}
    @Test public void profileUsesSeparateJsonFramesAndByteLength() {
        var commands=StockProtocol.profile("小卡","Hi");
        byte[] name="{\"nickname\":\"小卡\"}".getBytes(StandardCharsets.UTF_8);
        byte[] frame=commands.get(0).frame();assertEquals(name.length,frame[2]&255);assertEquals(0,frame[3]);
        assertArrayEquals(name,java.util.Arrays.copyOfRange(frame,4,frame.length));
        assertArrayEquals(new byte[]{1,1,14,0,'{','"','i','n','t','r','o','"',':','"','H','i','"','}'},commands.get(1).frame());
        assertEquals(1,commands.get(0).successStatus());
    }
    @Test public void rejectsSilentStockTextTruncationAndMalformedUnicode() {
        assertEquals(2,StockProtocol.profile("n".repeat(47),"中".repeat(28)).size());
        assertThrows(IllegalArgumentException.class,()->StockProtocol.profile("n".repeat(48),""));
        assertThrows(IllegalArgumentException.class,()->StockProtocol.profile("中".repeat(16),""));
        assertThrows(IllegalArgumentException.class,()->StockProtocol.profile("","a".repeat(29)));
        assertThrows(IllegalArgumentException.class,()->StockProtocol.profile("","😀".repeat(22)));
        assertThrows(IllegalArgumentException.class,()->StockProtocol.profile("\ud800",""));
        assertThrows(IllegalArgumentException.class,()->StockProtocol.profile("","\udc00"));
        assertThrows(IllegalArgumentException.class,()->StockProtocol.profile("a\0b",""));
        // Valid profile text may still exceed JSON256 after escaping; reject instead of split/truncate.
        assertThrows(IllegalArgumentException.class,()->StockProtocol.profile("\u0001".repeat(47),""));
        byte[] escaped=StockProtocol.profile("\"\\\n","").get(0).frame();
        String json=new String(escaped,4,escaped.length-4,StandardCharsets.UTF_8);
        assertTrue(json.contains("\\\"\\\\\\u000a"));
    }
    @Test public void independentImageFixturesModeBeginChunkEnd() {
        byte[] jpeg=jpeg(256);var commands=StockProtocol.image(StockProtocol.ImageMode.AVATAR,jpeg,256);
        assertEquals(5,commands.size());
        assertEquals("{\"img_mode\":\"avatar\"}",new String(commands.get(0).frame(),4,21,StandardCharsets.UTF_8));
        assertArrayEquals(new byte[]{1,2,5,0,0,0,1,0,0},commands.get(1).frame());
        byte[] data=commands.get(2).frame();assertEquals(260,data.length);assertEquals(0,data[2]);assertEquals(1,data[3]);assertEquals(1,data[4]);assertEquals((byte)0xff,data[5]);assertEquals((byte)0xd8,data[6]);
        assertArrayEquals(new byte[]{1,2,2,0,1,(byte)0xd9},commands.get(3).frame());
        assertArrayEquals(new byte[]{1,2,1,0,2},commands.get(4).frame());assertEquals(0,commands.get(4).successStatus());
        jpeg[0]=0;assertEquals((byte)0xff,commands.get(2).frame()[5]);
    }
    @Test public void mtu23And247And517PreserveEveryFrameBoundary() throws Exception {
        var commands=StockProtocol.image(StockProtocol.ImageMode.FULLSCREEN,jpeg(510),510);
        for(int mtu:new int[]{23,247,517})for(var command:commands){
            List<byte[]> writes=StockProtocol.fragments(command,mtu);ByteArrayOutputStream reconstructed=new ByteArrayOutputStream();
            for(byte[] write:writes){assertTrue(write.length>0&&write.length<=mtu-3);reconstructed.write(write);}
            assertArrayEquals(command.frame(),reconstructed.toByteArray());
        }
        assertEquals(13,StockProtocol.fragments(commands.get(2),23).size());
        assertEquals(2,StockProtocol.fragments(commands.get(2),247).size());
        assertEquals(1,StockProtocol.fragments(commands.get(2),517).size());
        assertThrows(IllegalArgumentException.class,()->StockProtocol.fragments(commands.get(0),22));
        assertThrows(IllegalArgumentException.class,()->StockProtocol.fragments(commands.get(0),518));
    }
    @Test public void validatesImageLimitsAndFrameLimits() {
        assertThrows(IllegalArgumentException.class,()->StockProtocol.image(StockProtocol.ImageMode.AVATAR,jpeg(256),255));
        assertThrows(IllegalArgumentException.class,()->StockProtocol.image(StockProtocol.ImageMode.AVATAR,new byte[8],8));
        assertThrows(IllegalArgumentException.class,()->StockProtocol.encode(1,new byte[257]));
        byte[] maximum=StockProtocol.encode(1,new byte[256]);assertEquals(260,maximum.length);assertEquals(1,maximum[3]);
    }
}
