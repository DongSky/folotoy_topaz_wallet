package com.folotoy.passport.wallet.ble;

import org.junit.Test;
import static org.junit.Assert.*;
import java.util.List;

public class StockTransferSessionTest {
    private StockTransferSession profile(int mtu){return new StockTransferSession(StockProtocol.profile("Alice","Hello"),mtu,1000);}
    private void ackCommand(StockTransferSession session,int mtu,int type,int status,long now) {
        StockTransferSession.Write write;
        while((write=session.nextWrite(now))!=null){assertTrue(write.bytes().length<=mtu-3);session.onWriteComplete(write.token(),true,now);}
        assertEquals(StockTransferSession.State.WAITING_RESPONSE,session.state());
        session.onNotification(new byte[]{(byte)type,(byte)status},now);
    }
    @Test public void oneWriteAtATimeAndWrongOrLateCallbacksNeverAdvance() {
        StockTransferSession s=profile(23);var first=s.nextWrite(0);assertEquals(20,first.bytes().length);assertNull(s.nextWrite(1));
        s.onWriteComplete(first.token()+1,true,2);assertEquals(StockTransferSession.State.WRITING,s.state());
        s.onNotification(new byte[]{1,1},3); // Early same-type notification before final fragment is ignored.
        s.onWriteComplete(first.token(),true,4);var last=s.nextWrite(5);assertEquals(4,last.bytes().length);
        s.onWriteComplete(first.token(),true,6);assertEquals(StockTransferSession.State.WRITING,s.state());
        s.onWriteComplete(last.token(),true,7);s.onNotification(new byte[]{2,0},8);
        assertEquals(StockTransferSession.State.WAITING_RESPONSE,s.state());
        s.onNotification(new byte[]{1,1,0},9);assertEquals(0,s.acknowledgedCommands());
        s.onNotification(new byte[]{1,1},10);assertEquals(1,s.acknowledgedCommands());
        s.onNotification(new byte[]{1,1},11);assertEquals(1,s.acknowledgedCommands()); // duplicate between commands ignored
        ackCommand(s,23,1,1,12);assertEquals(StockTransferSession.State.TRANSPORT_COMPLETE,s.state());assertFalse(s.renderingConfirmed());
    }
    @Test public void notificationBeforeFinalWriteCallbackWaitsForWriteSuccess() {
        StockTransferSession s=profile(247);var write=s.nextWrite(0);s.onNotification(new byte[]{1,1},1);
        assertEquals(StockTransferSession.State.WRITING,s.state());assertEquals(0,s.acknowledgedCommands());assertNull(s.nextWrite(2));
        s.onWriteComplete(write.token(),true,3);assertEquals(1,s.acknowledgedCommands());
        StockTransferSession failed=profile(247);var pending=failed.nextWrite(0);failed.onNotification(new byte[]{1,1},1);
        failed.onWriteComplete(pending.token(),false,2);assertEquals(StockTransferSession.State.UNKNOWN,failed.state());
    }
    @Test public void imageUsesTypeSpecificAcknowledgementAndEndNeverProvesRendering() {
        for(int mtu:new int[]{23,247,517}){
            var commands=StockProtocol.image(StockProtocol.ImageMode.AVATAR,StockProtocolTest.jpeg(256),256);
            StockTransferSession s=new StockTransferSession(commands,mtu,1000);
            ackCommand(s,mtu,1,1,0);for(int i=1;i<commands.size();i++)ackCommand(s,mtu,2,0,i);
            assertEquals(StockTransferSession.State.TRANSPORT_COMPLETE,s.state());assertEquals(commands.size(),s.acknowledgedCommands());assertFalse(s.renderingConfirmed());
        }
        var image=StockProtocol.image(StockProtocol.ImageMode.AVATAR,StockProtocolTest.jpeg(4),4);
        StockTransferSession s=new StockTransferSession(image,247,1000);ackCommand(s,247,1,1,0);ackCommand(s,247,2,1,1);
        assertEquals(StockTransferSession.State.REJECTED,s.state());assertEquals(2,s.rejectedType());assertEquals(1,s.rejectedStatus());
    }
    @Test public void rejectedBusyAndGlobalParserErrorsStopWithoutRetries() {
        StockTransferSession s=profile(23);s.nextWrite(0);s.onNotification(new byte[]{0,0x11},1);
        assertEquals(StockTransferSession.State.REJECTED,s.state());assertNull(s.nextWrite(2));
        assertEquals(0x11,s.rejectedStatus());
        var image=StockProtocol.image(StockProtocol.ImageMode.FULLSCREEN,StockProtocolTest.jpeg(4),4);
        StockTransferSession busy=new StockTransferSession(image,247,1000);ackCommand(busy,247,1,1,0);ackCommand(busy,247,2,0x1b,1);
        assertEquals(StockTransferSession.State.REJECTED,busy.state());assertEquals(1,busy.acknowledgedCommands());
    }
    @Test public void timeoutCancellationAndLostEndAckHaveUnknownOutcome() {
        StockTransferSession notStarted=profile(247);notStarted.cancel();assertEquals(StockTransferSession.State.CANCELLED,notStarted.state());
        StockTransferSession cancelled=profile(247);cancelled.nextWrite(0);cancelled.cancel();assertEquals(StockTransferSession.State.UNKNOWN,cancelled.state());
        StockTransferSession partial=profile(23);var fragment=partial.nextWrite(0);partial.onWriteComplete(fragment.token(),true,1);partial.checkTimeout(1001);
        assertEquals(StockTransferSession.State.UNKNOWN,partial.state());assertNull(partial.nextWrite(1002));
        var commands=StockProtocol.image(StockProtocol.ImageMode.AVATAR,StockProtocolTest.jpeg(4),4);
        StockTransferSession end=new StockTransferSession(commands,247,1000);ackCommand(end,247,1,1,0);ackCommand(end,247,2,0,1);ackCommand(end,247,2,0,2);
        var last=end.nextWrite(3);assertArrayEquals(new byte[]{1,2,1,0,2},last.bytes());end.onWriteComplete(last.token(),true,4);
        end.checkTimeout(1003);assertEquals(StockTransferSession.State.UNKNOWN,end.state());
        end.onNotification(new byte[]{2,0},1004);assertEquals(StockTransferSession.State.UNKNOWN,end.state());assertFalse(end.renderingConfirmed());
    }
    @Test public void jsonRequiresStatusOneAndNoNotificationCannotAdvance() {
        StockTransferSession s=profile(247);var write=s.nextWrite(0);s.onWriteComplete(write.token(),true,1);
        assertNull(s.nextWrite(2));assertEquals(0,s.acknowledgedCommands());
        s.onNotification(new byte[]{1,0},3);assertEquals(StockTransferSession.State.REJECTED,s.state());
        assertEquals(0,s.rejectedStatus());assertEquals(1,s.rejectedType());
    }
    @Test public void monotonicClockAndImmutableWriteBytes() {
        StockTransferSession s=profile(247);var write=s.nextWrite(10);byte[] bytes=write.bytes();bytes[0]=99;assertEquals(1,write.bytes()[0]);
        assertThrows(IllegalArgumentException.class,()->s.checkTimeout(9));
    }
}
