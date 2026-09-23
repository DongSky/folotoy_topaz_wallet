package com.folotoy.passport.wallet.ble;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.zip.CRC32;

public final class BleProtocol {
    public static final UUID SERVICE=UUID.fromString("f0100001-6c6f-4c65-9a55-706173737074"), WRITE=UUID.fromString("f0100002-6c6f-4c65-9a55-706173737074"), STATUS=UUID.fromString("f0100003-6c6f-4c65-9a55-706173737074");
    public static final byte BEGIN=1,DATA=2,COMMIT=3,ABORT=4;
    public enum Action{POLL,SEND_NEXT,DONE,FAIL}
    public record Status(int state,int error,long received,long expected){}
    private BleProtocol(){}
    public static List<byte[]> frames(byte[] packageBytes,int mtuPayload){
        if(mtuPayload<6)throw new IllegalArgumentException("MTU payload too small");CRC32 crc=new CRC32();crc.update(packageBytes);
        List<byte[]> out=new ArrayList<>();ByteBuffer begin=ByteBuffer.allocate(9).order(ByteOrder.LITTLE_ENDIAN);begin.put(BEGIN).putInt(packageBytes.length).putInt((int)crc.getValue());out.add(begin.array());
        int chunk=mtuPayload-5;for(int off=0;off<packageBytes.length;off+=chunk){int n=Math.min(chunk,packageBytes.length-off);ByteBuffer d=ByteBuffer.allocate(5+n).order(ByteOrder.LITTLE_ENDIAN);d.put(DATA).putInt(off).put(packageBytes,off,n);out.add(d.array());}out.add(new byte[]{COMMIT});return out;
    }
    public static Status parseStatus(byte[] b){if(b==null||b.length!=12)throw new IllegalArgumentException("Status must be 12 bytes");ByteBuffer x=ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN);int state=x.get()&255,error=x.get()&255;if(x.getShort()!=0||state>5)throw new IllegalArgumentException("Malformed status");return new Status(state,error,Integer.toUnsignedLong(x.getInt()),Integer.toUnsignedLong(x.getInt()));}
    public static Action next(Status s,long sent){if(s.state()==4||s.error()!=0)return Action.FAIL;if(s.state()==3)return Action.DONE;if(s.state()==5||s.state()==1||s.state()==0)return Action.POLL;if(s.state()==2&&s.received()==sent)return Action.SEND_NEXT;return Action.POLL;}
    /** Validate the actual acknowledged command; commit alone can produce DONE. */
    public static Action nextForFrame(Status s,byte type,long packageLength,long expectedAfter) {
        if(s.error()!=0||s.state()==4)return Action.FAIL;
        // BEGIN is enqueued on the device worker: an immediate read may still
        // describe its previously committed (larger) package until erase starts.
        if(type==BEGIN&&s.state()==3)return Action.POLL;
        if(s.received()>packageLength||s.expected()>packageLength)return Action.FAIL;
        if(s.state()==5)return Action.POLL;
        if(type==BEGIN)return (s.state()==1||s.state()==2)&&s.received()==0&&s.expected()==packageLength?Action.SEND_NEXT:Action.POLL;
        if(s.expected()!=packageLength)return Action.FAIL;
        if(type==COMMIT)return s.state()==3&&s.received()==packageLength?Action.DONE:Action.POLL;
        if(type==DATA){if(s.received()>expectedAfter)return Action.FAIL;return s.state()==2&&s.received()==expectedAfter?Action.SEND_NEXT:Action.POLL;}
        return Action.FAIL;
    }

    /** BEGIN can observe the authenticated pre-write status until the worker
     * consumes it. Ignore an identical old error only until the first changed
     * status, and never past a bounded deadline. The wire has no transaction ID. */
    public static final class BeginObservation {
        private final Status baseline;
        private final long startedAt,timeoutMs;
        private boolean changed,ambiguousTimeout;
        public BeginObservation(Status baseline,long startedAt,long timeoutMs) {
            if(baseline==null||startedAt<0||timeoutMs<=0)throw new IllegalArgumentException("Invalid BEGIN observation");
            this.baseline=baseline;this.startedAt=startedAt;this.timeoutMs=timeoutMs;
        }
        public Action observe(Status current,long packageLength,long nowMs) {
            if(nowMs<startedAt)throw new IllegalArgumentException("Use a monotonic clock");
            if(!changed&&current.equals(baseline)&&(baseline.state()==4||baseline.error()!=0)) {
                if(nowMs-startedAt>=timeoutMs){ambiguousTimeout=true;return Action.FAIL;}
                return Action.POLL;
            }
            if(!current.equals(baseline))changed=true;
            return nextForFrame(current,BEGIN,packageLength,0);
        }
        public boolean ambiguousTimeout(){return ambiguousTimeout;}
    }

}
