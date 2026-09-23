package com.folotoy.passport.wallet.ble;

import java.util.List;

/** Pure sequential write-with-response/notification coordinator. Feed monotonic milliseconds.
 * One session per subscribed connection; never reuse a connection after an uncertain failure.
 * Stock ACKs carry no transaction/sequence ID: a duplicated same-type ACK during a later
 * final fragment cannot be distinguished. No retries, pipelining or automatic recovery.
 * All methods must be called on one serialized owner thread. */
public final class StockTransferSession {
    public enum State { READY, WRITING, WAITING_RESPONSE, TRANSPORT_COMPLETE, REJECTED, UNKNOWN, CANCELLED }
    public record Write(long token,byte[] bytes) {
        public Write { bytes=bytes.clone(); }
        @Override public byte[] bytes(){return bytes.clone();}
    }
    private final List<StockProtocol.Command> commands;
    private final int mtu;
    private final long timeoutMs;
    private State state=State.READY;
    private List<byte[]> fragments;
    private int commandIndex,fragmentIndex;
    private long token,startedAt,lastNow=-1;
    private boolean dispatched;
    private Integer pendingStatus;
    private int rejectedType=-1,rejectedStatus=-1;

    public StockTransferSession(List<StockProtocol.Command> commands,int mtu,long timeoutMs) {
        if(commands==null||commands.isEmpty()||mtu<23||mtu>517||timeoutMs<=0)
            throw new IllegalArgumentException("Commands, MTU23..517 and positive timeout required");
        this.commands=List.copyOf(commands);this.mtu=mtu;this.timeoutMs=timeoutMs;
        fragments=StockProtocol.fragments(this.commands.get(0),mtu);
    }
    public State state(){return state;}
    public int acknowledgedCommands(){return commandIndex;}
    public int totalCommands(){return commands.size();}
    public int rejectedType(){return rejectedType;}
    public int rejectedStatus(){return rejectedStatus;}
    public boolean renderingConfirmed(){return false;}
    public boolean terminal(){return state==State.TRANSPORT_COMPLETE||state==State.REJECTED||state==State.UNKNOWN||state==State.CANCELLED;}

    /** Null means wait/terminal. Caller must issue exactly this ATT write and return its token. */
    public Write nextWrite(long nowMs) {
        checkTimeout(nowMs);if(state!=State.READY)return null;
        state=State.WRITING;startedAt=nowMs;dispatched=true;
        return new Write(++token,fragments.get(fragmentIndex));
    }
    public void onWriteComplete(long writeToken,boolean success,long nowMs) {
        checkTimeout(nowMs);if(state!=State.WRITING||writeToken!=token)return;
        if(!success){state=State.UNKNOWN;pendingStatus=null;return;}
        if(fragmentIndex+1<fragments.size()){
            fragmentIndex++;state=State.READY;startedAt=nowMs;return;
        }
        state=State.WAITING_RESPONSE; // Deadline remains tied to final write, not callback arrival.
        if(pendingStatus!=null){int status=pendingStatus;pendingStatus=null;accept(status,nowMs);}
    }
    public void onNotification(byte[] response,long nowMs) {
        checkTimeout(nowMs);if(terminal()||response==null||response.length!=2)return;
        int type=response[0]&255,status=response[1]&255;
        // Global parser errors can arrive while a fragmented frame is still being written.
        if(type==0&&(status==0x10||status==0x11||status==0x15)&&dispatched){
            rejectedType=type;rejectedStatus=status;state=State.REJECTED;pendingStatus=null;return;
        }
        if(type!=commands.get(commandIndex).type())return;
        if(state==State.WAITING_RESPONSE)accept(status,nowMs);
        else if(state==State.WRITING&&fragmentIndex==fragments.size()-1&&pendingStatus==null)
            pendingStatus=status; // Notifications may reach Android before final onCharacteristicWrite.
    }
    private void accept(int status,long nowMs) {
        StockProtocol.Command current=commands.get(commandIndex);
        if(status!=current.successStatus()){
            rejectedType=current.type();rejectedStatus=status;state=State.REJECTED;return;
        }
        commandIndex++;fragmentIndex=0;pendingStatus=null;startedAt=nowMs;
        if(commandIndex==commands.size()){state=State.TRANSPORT_COMPLETE;return;}
        fragments=StockProtocol.fragments(commands.get(commandIndex),mtu);state=State.READY;
    }
    public void checkTimeout(long nowMs) {
        if(nowMs<0||nowMs<lastNow)throw new IllegalArgumentException("Use a monotonic nonnegative clock");
        lastNow=nowMs;
        if(!terminal()&&dispatched&&nowMs-startedAt>=timeoutMs){state=State.UNKNOWN;pendingStatus=null;}
    }
    /** The caller must disconnect/clear the stock parser; there is no verified image abort opcode. */
    public void cancel(){if(!terminal()){state=dispatched?State.UNKNOWN:State.CANCELLED;pendingStatus=null;}}
}
