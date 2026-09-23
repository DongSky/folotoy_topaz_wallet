package com.folotoy.passport.wallet.ble;

import android.annotation.SuppressLint;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.Context;
import android.location.LocationManager;
import android.os.*;
import java.util.*;

/** Foreground PCW capability discovery followed by owner-authenticated sequential transfer. */
@SuppressLint("MissingPermission")
public final class WalletBleClient extends BluetoothGattCallback {
    public enum State { IDLE, SCANNING, SELECT_DEVICE, CONNECTING, CHECKING_CAPABILITY, PAIRING, AUTHENTICATING, READY, SENDING, COMPLETE, ERROR }
    public interface Listener {void state(State state,String message);void devices(List<BluetoothDevice> devices);void complete();void error(String message);}
    private enum Operation { NONE, DISCOVER, MTU, READ, WRITE }
    private final Context context;private final Listener listener;private final Handler handler=new Handler(Looper.getMainLooper());
    private final Map<String,BluetoothDevice> discovered=new LinkedHashMap<>();
    private BluetoothLeScanner scanner;private ScanCallback scanCallback;private BluetoothGatt gatt;
    private BluetoothGattCharacteristic write,status;private Operation operation=Operation.NONE;
    private List<byte[]> frames;private int frame,mtuPayload=20,generation;private long expectedLength,lastProgress,deadline,observedReceived;
    private BleProtocol.Status readinessStatus;private BleProtocol.BeginObservation beginObservation;
    private boolean readinessRead,closed;private int observedState=-1;private State state=State.IDLE;
    public WalletBleClient(Context context,Listener listener){this.context=context;this.listener=listener;}
    private void setState(State next,String message){state=next;listener.state(next,message);}
    public boolean ready(){return readinessRead&&gatt!=null&&frames==null;}
    public void scan(){close();closed=false;int token=generation;
        try{BluetoothManager manager=context.getSystemService(BluetoothManager.class);BluetoothAdapter adapter=manager==null?null:manager.getAdapter();
            if(adapter==null||!adapter.isEnabled()){failure("请打开手机蓝牙后重试。");return;}
            if(Build.VERSION.SDK_INT<31){LocationManager location=context.getSystemService(LocationManager.class);if(location==null||(!location.isProviderEnabled(LocationManager.GPS_PROVIDER)&&!location.isProviderEnabled(LocationManager.NETWORK_PROVIDER))){failure("此 Android 版本搜索需要打开系统位置信息。");return;}}
            discovered.clear();scanner=adapter.getBluetoothLeScanner();if(scanner==null){failure("蓝牙搜索暂不可用。");return;}
            scanCallback=new ScanCallback(){
                @Override public void onScanResult(int type,ScanResult result){handler.post(()->found(token,result));}
                @Override public void onBatchScanResults(List<ScanResult> batch){handler.post(()->{for(ScanResult r:batch)found(token,r);});}
                @Override public void onScanFailed(int code){handler.post(()->{if(token==generation&&!closed&&state==State.SCANNING)failure("搜索失败，请稍后重试。");});}
            };
            setState(State.SCANNING,"正在搜索 Passport…");scanner.startScan(null,new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),scanCallback);
            handler.postDelayed(()->{if(closed||token!=generation||state!=State.SCANNING)return;stopScan();if(discovered.isEmpty()){failure("未找到设备，请唤醒 Passport，并先断开微信连接。");return;}setState(State.SELECT_DEVICE,"选择你的 Passport，连接后检查钱包同步能力。");listener.devices(new ArrayList<>(discovered.values()));},8000);
        }catch(SecurityException|IllegalStateException e){failure("蓝牙已关闭或权限不足，请检查系统设置。");}
    }
    private void found(int token,ScanResult result){if(closed||token!=generation||state!=State.SCANNING)return;
        ScanRecord record=result.getScanRecord();String name=record==null?null:record.getDeviceName();
        boolean service=record!=null&&record.getServiceUuids()!=null&&record.getServiceUuids().contains(new ParcelUuid(StockProtocol.SERVICE));
        if(service||(name!=null&&name.matches("(?i)[0-9a-f]{12}")))discovered.put(result.getDevice().getAddress(),result.getDevice());
    }
    public void connectSelected(BluetoothDevice device){close();closed=false;setState(State.CONNECTING,"正在连接…");
        try{gatt=device.connectGatt(context,false,this,BluetoothDevice.TRANSPORT_LE);if(gatt==null){failure("无法发起连接。");return;}int token=generation;
            handler.postDelayed(()->{if(!closed&&token==generation&&!readinessRead)failure("连接或配对超时。请在设备长按上键开启两分钟同步窗口后重试。");},90000);
        }catch(SecurityException|IllegalStateException e){failure("无法连接，请检查附近设备权限。");}
    }
    private boolean current(BluetoothGatt source){return !closed&&source==gatt;}
    @Override public void onConnectionStateChange(BluetoothGatt source,int code,int connected){handler.post(()->{
        if(!current(source))return;if(code!=BluetoothGatt.GATT_SUCCESS||connected==BluetoothProfile.STATE_DISCONNECTED){failure(commitUncertain()?uncertain():"连接中断；未完成的钱包传输已停止，请重新连接。");return;}
        if(connected!=BluetoothProfile.STATE_CONNECTED)return;
        try{operation=Operation.DISCOVER;setState(State.CHECKING_CAPABILITY,"检查设备是否支持钱包页面同步…");if(!source.discoverServices())failure("无法发现设备服务。");}
        catch(SecurityException|IllegalStateException e){failure("蓝牙或权限发生变化。");}
    });}
    @Override public void onServicesDiscovered(BluetoothGatt source,int code){handler.post(()->{
        if(!current(source)||operation!=Operation.DISCOVER)return;operation=Operation.NONE;
        BluetoothGattService service=source.getService(BleProtocol.SERVICE);
        if(code!=BluetoothGatt.GATT_SUCCESS||service==null){failure("此固件未提供钱包同步服务。原版固件可使用“资料与图片传输”；本次未发起配对或写入。");return;}
        write=service.getCharacteristic(BleProtocol.WRITE);status=service.getCharacteristic(BleProtocol.STATUS);
        if(write==null||status==null||(write.getProperties()&BluetoothGattCharacteristic.PROPERTY_WRITE)==0||(status.getProperties()&BluetoothGattCharacteristic.PROPERTY_READ)==0){failure("钱包服务不完整，无法同步。");return;}
        try{BluetoothDevice device=source.getDevice();if(device.getBondState()==BluetoothDevice.BOND_BONDED){configureMtu();return;}
            setState(State.PAIRING,"在设备长按上键开启同步窗口，按 OK 允许配对，再在 Android 提示中输入设备显示的六位码。更换手机需先在设备长按下键并确认。");
            if(device.getBondState()==BluetoothDevice.BOND_NONE&&!device.createBond()){failure("无法发起配对，请先长按设备上键开启同步窗口。");return;}awaitBond(source,generation);
        }catch(SecurityException|IllegalStateException e){failure("无法配对，请检查蓝牙权限。");}
    });}
    private void awaitBond(BluetoothGatt target,int token){if(closed||target!=gatt||token!=generation)return;
        try{if(target.getDevice().getBondState()==BluetoothDevice.BOND_BONDED){configureMtu();return;}
            handler.postDelayed(()->awaitBond(target,token),250);
        }catch(SecurityException e){failure("配对权限已撤销。");}
    }
    private void configureMtu(){try{setState(State.AUTHENTICATING,"验证已配对手机的钱包访问权限…");operation=Operation.MTU;
        if(!gatt.requestMtu(247)){operation=Operation.NONE;readStatusLater(0);}
    }catch(SecurityException|IllegalStateException e){failure("无法配置安全连接。");}}
    @Override public void onMtuChanged(BluetoothGatt source,int size,int code){handler.post(()->{if(!current(source)||operation!=Operation.MTU)return;operation=Operation.NONE;
        mtuPayload=code==BluetoothGatt.GATT_SUCCESS?Math.max(20,Math.min(512,size-3)):20;readStatusLater(0);
    });}
    public void transfer(byte[] packageBytes){if(!ready()){listener.error("安全连接尚未就绪，请重新连接。");return;}
        if(packageBytes==null||packageBytes.length<32||packageBytes.length>32+52*27716){listener.error("钱包页面包超过设备 52 页容量。");return;}
        frames=BleProtocol.frames(packageBytes,mtuPayload);frame=0;expectedLength=packageBytes.length;observedReceived=0;observedState=-1;
        lastProgress=SystemClock.elapsedRealtime();beginObservation=new BleProtocol.BeginObservation(readinessStatus,lastProgress,10000);deadline=lastProgress+Math.max(120000L,frames.size()*1000L);int token=generation;
        setState(State.SENDING,"正在准备设备存储…");watchdog(token);writeFrame();
    }
    private void watchdog(int token){handler.postDelayed(()->{if(closed||token!=generation||frames==null)return;
        long now=SystemClock.elapsedRealtime();if(now>=deadline||now-lastProgress>25000){failure(commitUncertain()?uncertain():"设备长时间未取得传输进展。连接已断开，请重新同步。");return;}watchdog(token);
    },1000);}
    @SuppressWarnings("deprecation") private void writeFrame(){if(frames==null||operation!=Operation.NONE)return;byte[] bytes=frames.get(frame);operation=Operation.WRITE;
        try{boolean accepted;if(Build.VERSION.SDK_INT>=33)accepted=gatt.writeCharacteristic(write,bytes,BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)==BluetoothStatusCodes.SUCCESS;
            else{write.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);write.setValue(bytes);accepted=gatt.writeCharacteristic(write);}
            if(!accepted)failure(commitUncertain()?uncertain():"Android 未能提交数据，连接已断开。");
        }catch(SecurityException|IllegalStateException e){failure(commitUncertain()?uncertain():"蓝牙写入中断，请重新连接。");}
    }
    private void readStatusLater(long delay){BluetoothGatt target=gatt;int token=generation;handler.postDelayed(()->{
        if(closed||token!=generation||target!=gatt||operation!=Operation.NONE)return;
        try{operation=Operation.READ;if(!target.readCharacteristic(status))failure(commitUncertain()?uncertain():"无法查询设备状态。");}
        catch(SecurityException|IllegalStateException e){failure(commitUncertain()?uncertain():"设备状态查询中断。");}
    },delay);}
    @Override public void onCharacteristicWrite(BluetoothGatt source,BluetoothGattCharacteristic characteristic,int code){handler.post(()->{
        if(!current(source)||characteristic!=write||operation!=Operation.WRITE||frames==null)return;operation=Operation.NONE;
        if(code!=BluetoothGatt.GATT_SUCCESS){failure(commitUncertain()?uncertain():"安全写入被拒绝。请确认手机配对与设备同步窗口。");return;}
        readStatusLater(0);
    });}
    @Override public void onCharacteristicRead(BluetoothGatt source,BluetoothGattCharacteristic characteristic,byte[] bytes,int code){received(source,characteristic,bytes==null?null:bytes.clone(),code);}
    @SuppressWarnings("deprecation") @Override public void onCharacteristicRead(BluetoothGatt source,BluetoothGattCharacteristic characteristic,int code){byte[] bytes=characteristic.getValue();received(source,characteristic,bytes==null?null:bytes.clone(),code);}
    private void received(BluetoothGatt source,BluetoothGattCharacteristic characteristic,byte[] bytes,int code){handler.post(()->{
        if(!current(source)||characteristic!=status||operation!=Operation.READ)return;operation=Operation.NONE;
        if(code!=BluetoothGatt.GATT_SUCCESS){failure(commitUncertain()?uncertain():"设备拒绝安全访问。请开启同步窗口；如已更换手机，先在设备长按下键解除旧配对。");return;}
        final BleProtocol.Status reported;try{reported=BleProtocol.parseStatus(bytes);}catch(Exception e){failure("设备状态格式错误。");return;}
        if(frames==null){readinessStatus=reported;readinessRead=true;setState(State.READY,"安全连接已就绪，尚未发送页面。请点击同步。");return;}
        long now=SystemClock.elapsedRealtime();
        if(reported.received()>observedReceived||reported.state()!=observedState){observedReceived=reported.received();observedState=reported.state();lastProgress=now;}
        byte type=frames.get(frame)[0];long expectedAfter=type==BleProtocol.DATA?offsetAfter(frames.get(frame)):0;
        BleProtocol.Action action=type==BleProtocol.BEGIN?beginObservation.observe(reported,expectedLength,now):BleProtocol.nextForFrame(reported,type,expectedLength,expectedAfter);
        if(action==BleProtocol.Action.FAIL&&type==BleProtocol.BEGIN&&beginObservation.ambiguousTimeout()){failure("设备仍报告连接前的旧错误，无法确认新传输是否开始。已断开，请检查设备后重试。");return;}
        if(action==BleProtocol.Action.FAIL){failure("设备拒绝钱包包（错误 "+reported.error()+"）。旧页面是否保留请以设备为准；请重新连接。");return;}
        if(action==BleProtocol.Action.DONE){frames=null;readinessRead=false;setState(State.COMPLETE,"设备已校验并提交完整页面包。");listener.complete();close();return;}
        if(action==BleProtocol.Action.POLL){setState(State.SENDING,reported.state()==5?"设备正在擦写或校验，请保持连接…":"等待设备确认已发送的数据…");readStatusLater(reported.state()==5?150:60);return;}
        lastProgress=now;frame++;setState(State.SENDING,"正在同步 · "+Math.min(99,100L*reported.received()/expectedLength)+"%");writeFrame();
    });}
    private static long offsetAfter(byte[] bytes){return Integer.toUnsignedLong(java.nio.ByteBuffer.wrap(bytes,1,4).order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt())+bytes.length-5;}
    private boolean commitUncertain(){return frames!=null&&frame<frames.size()&&frames.get(frame)[0]==BleProtocol.COMMIT;}
    private static String uncertain(){return "提交结果不确定：设备可能已保存新页面。请查看设备，重新连接后再决定是否同步。";}
    private void failure(String message){close();setState(State.ERROR,message);listener.error(message);}
    private void stopScan(){if(scanner!=null){try{if(scanCallback!=null)scanner.stopScan(scanCallback);}catch(SecurityException|IllegalStateException ignored){}scanner=null;scanCallback=null;}}
    public void close(){closed=true;generation++;stopScan();handler.removeCallbacksAndMessages(null);BluetoothGatt old=gatt;gatt=null;operation=Operation.NONE;write=null;status=null;frames=null;frame=0;expectedLength=0;readinessRead=false;readinessStatus=null;beginObservation=null;mtuPayload=20;
        if(old!=null){try{old.disconnect();}catch(SecurityException|IllegalStateException ignored){}try{old.close();}catch(SecurityException|IllegalStateException ignored){}}
    }
}
