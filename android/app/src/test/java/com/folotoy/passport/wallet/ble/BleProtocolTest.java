package com.folotoy.passport.wallet.ble;

import static org.junit.Assert.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.List;
import org.junit.Test;

public class BleProtocolTest {
    @Test public void transferFramesAreSequentialAndBounded() {
        byte[] payload = new byte[100];
        List<byte[]> frames = BleProtocol.frames(payload, 20);
        assertEquals(BleProtocol.BEGIN, frames.get(0)[0]);
        int offset = 0;
        for (int i = 1; i < frames.size() - 1; i++) {
            byte[] f = frames.get(i); assertEquals(BleProtocol.DATA, f[0]);
            assertEquals(offset, ByteBuffer.wrap(f, 1, 4).order(ByteOrder.LITTLE_ENDIAN).getInt());
            offset += f.length - 5;
            assertTrue(f.length <= 20);
        }
        assertEquals(100, offset);
        assertArrayEquals(new byte[]{BleProtocol.COMMIT}, frames.get(frames.size()-1));
    }

    @Test public void busyAndReceivingStatusAreNotErrors() {
        assertEquals(BleProtocol.Action.POLL, BleProtocol.next(new BleProtocol.Status(5,0,0,100), 100));
        assertEquals(BleProtocol.Action.SEND_NEXT, BleProtocol.next(new BleProtocol.Status(2,0,40,100), 40));
        assertEquals(BleProtocol.Action.FAIL, BleProtocol.next(new BleProtocol.Status(4,3,40,100), 40));
    }

    @Test(expected=IllegalArgumentException.class)
    public void malformedStatusRejected() { BleProtocol.parseStatus(new byte[11]); }
    @Test public void commitIsTheOnlyCompletionAndMustMatchExactLength() {
        assertEquals(BleProtocol.Action.DONE,BleProtocol.nextForFrame(new BleProtocol.Status(3,0,100,100),BleProtocol.COMMIT,100,0));
        assertEquals(BleProtocol.Action.POLL,BleProtocol.nextForFrame(new BleProtocol.Status(3,0,99,100),BleProtocol.COMMIT,100,0));
        assertEquals(BleProtocol.Action.POLL,BleProtocol.nextForFrame(new BleProtocol.Status(3,0,100,100),BleProtocol.BEGIN,100,0));
        assertEquals(BleProtocol.Action.POLL,BleProtocol.nextForFrame(new BleProtocol.Status(3,0,1000,1000),BleProtocol.BEGIN,100,0));
        assertEquals(BleProtocol.Action.POLL,BleProtocol.nextForFrame(new BleProtocol.Status(3,0,100,100),BleProtocol.DATA,100,100));
        assertEquals(BleProtocol.Action.FAIL,BleProtocol.nextForFrame(new BleProtocol.Status(3,0,100,101),BleProtocol.COMMIT,100,0));
    }
    @Test public void commandSpecificOffsetAndBusyChecksPreventEarlyAdvance() {
        assertEquals(BleProtocol.Action.SEND_NEXT,BleProtocol.nextForFrame(new BleProtocol.Status(1,0,0,100),BleProtocol.BEGIN,100,0));
        assertEquals(BleProtocol.Action.POLL,BleProtocol.nextForFrame(new BleProtocol.Status(5,0,0,100),BleProtocol.BEGIN,100,0));
        assertEquals(BleProtocol.Action.POLL,BleProtocol.nextForFrame(new BleProtocol.Status(2,0,20,100),BleProtocol.DATA,100,40));
        assertEquals(BleProtocol.Action.SEND_NEXT,BleProtocol.nextForFrame(new BleProtocol.Status(2,0,40,100),BleProtocol.DATA,100,40));
        assertEquals(BleProtocol.Action.FAIL,BleProtocol.nextForFrame(new BleProtocol.Status(2,0,41,100),BleProtocol.DATA,100,40));
        assertEquals(BleProtocol.Action.FAIL,BleProtocol.nextForFrame(new BleProtocol.Status(2,7,40,100),BleProtocol.DATA,100,40));
    }

    @Test public void beginPollsExactPreviousCrcStorageAndTimeoutErrorsThenAcceptsWorkerReady() {
        for(int error:new int[]{6,7,9}){
            BleProtocol.Status old=new BleProtocol.Status(4,error,123,999);
            BleProtocol.BeginObservation pending=new BleProtocol.BeginObservation(old,100,10000);
            assertEquals(BleProtocol.Action.POLL,pending.observe(old,100,101));
            assertEquals(BleProtocol.Action.POLL,pending.observe(old,100,9999));
            assertEquals(BleProtocol.Action.POLL,pending.observe(new BleProtocol.Status(5,0,0,100),100,10000));
            assertEquals(BleProtocol.Action.SEND_NEXT,pending.observe(new BleProtocol.Status(1,0,0,100),100,10001));
            assertFalse(pending.ambiguousTimeout());
            // Old error reappearing after an observed transition is a current failure.
            assertEquals(BleProtocol.Action.FAIL,pending.observe(old,100,10002));
        }
    }
    @Test public void beginChangedFailureIsRejectedAndIdenticalFailureEventuallyBecomesUncertain() {
        BleProtocol.Status old=new BleProtocol.Status(4,6,123,999);
        BleProtocol.BeginObservation changed=new BleProtocol.BeginObservation(old,0,10000);
        assertEquals(BleProtocol.Action.FAIL,changed.observe(new BleProtocol.Status(4,7,123,999),100,1));
        assertFalse(changed.ambiguousTimeout());
        BleProtocol.BeginObservation unchanged=new BleProtocol.BeginObservation(old,0,10000);
        assertEquals(BleProtocol.Action.POLL,unchanged.observe(old,100,9999));
        assertEquals(BleProtocol.Action.FAIL,unchanged.observe(old,100,10000));
        assertTrue(unchanged.ambiguousTimeout());
        assertNotEquals(BleProtocol.Action.DONE,unchanged.observe(old,100,10001));
    }

}
