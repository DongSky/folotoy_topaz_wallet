package com.folotoy.passport.wallet;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.*;
import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.*;
import android.location.LocationManager;
import android.net.Uri;
import android.os.*;
import android.util.Base64;
import android.widget.*;
import com.folotoy.passport.wallet.ble.StockProtocol;
import com.folotoy.passport.wallet.ble.StockTransferSession;
import com.folotoy.passport.wallet.core.Profile;
import com.folotoy.passport.wallet.data.WalletRepository;
import com.folotoy.passport.wallet.importer.StockImageEncoder;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;
import static com.folotoy.passport.wallet.WalletUi.*;

/** Foreground stock profile/image sender. Never reads sensitive characteristic0012. */
@SuppressLint("MissingPermission")
public final class StockConnectActivity extends Activity {
    private static final UUID CCC=UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private static final int PICK_IMAGE=61;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final Map<String,BluetoothDevice> devices=new LinkedHashMap<>();
    private BluetoothLeScanner scanner;
    private ScanCallback scanCallback;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic command,response;
    private BluetoothGattDescriptor subscription;
    private StockTransferSession transfer;
    private TextView state,imageState;
    private LinearLayout results;
    private Button scan,sendProfile,sendAvatar,pickImage,sendImage,cancel;
    private boolean closed,scanning,ready,preparing,foreground,awaitingMtu,awaitingSubscription;
    private int generation,preparation,mtu=23;
    private long writeToken;
    private byte[] fullscreenJpeg;
    private String transferLabel;

    @Override public void onCreate(Bundle saved) {
        setTheme(android.R.style.Theme_Material_NoActionBar);super.onCreate(saved);
        getWindow().setStatusBarColor(BACKGROUND);getWindow().setNavigationBarColor(BACKGROUND);
        ScrollView scroll=new ScrollView(this);scroll.setFitsSystemWindows(true);scroll.setBackgroundColor(BACKGROUND);
        LinearLayout root=column(this);root.setPadding(dp(this,24),dp(this,24),dp(this,24),dp(this,28));scroll.addView(root);setContentView(scroll);
        add(root,button(this,"‹ 返回",false,v->finish()));add(root,text(this,"连接原版 Passport",25,true));
        add(root,copy("唤醒设备并靠近手机。若微信小程序已连接，请先在微信中断开。"));
        scan=button(this,"搜索设备",true,v->requestScan());add(root,scan);
        state=copy("尚未连接。选中候选设备后验证原版服务。");add(root,state);
        results=column(this);root.addView(results);
        add(root,text(this,"发送到设备",18,true));
        sendProfile=button(this,"发送当前昵称与简介",false,v->prepareProfile());add(root,sendProfile);
        sendAvatar=button(this,"发送当前头像",false,v->prepareAvatar());add(root,sendAvatar);
        pickImage=button(this,"选择全屏图片",false,v->startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("image/*").addCategory(Intent.CATEGORY_OPENABLE),PICK_IMAGE));add(root,pickImage);
        imageState=copy("图片将居中裁剪为 240 × 320。选择图片会断开蓝牙，返回后重新连接再发送。");add(root,imageState);
        sendImage=button(this,"发送已选全屏图片",false,v->confirmImage(StockProtocol.ImageMode.FULLSCREEN,fullscreenJpeg));add(root,sendImage);
        cancel=button(this,"断开连接",false,v->{boolean uncertain=transfer!=null;disconnect();state.setText(uncertain?"传输已中止，设备可能已收到部分内容。重新连接前请检查设备。":"已断开，可继续使用微信小程序。");});add(root,cancel);
        add(root,copy("这里发送昵称、简介和图片。新版固件的钱包、收款码与资产页面请从“同步名片与钱包”单独发送。设备确认接收不代表图片已经正确显示，请查看实机。"));controls();
    }
    private TextView copy(String message){TextView text=WalletUi.text(this,message,13,false);text.setTextColor(MUTED);return text;}
    private void add(LinearLayout parent,android.view.View view){LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.bottomMargin=dp(this,14);parent.addView(view,p);}
    private void controls(){if(scan==null)return;boolean idle=transfer==null&&!preparing;scan.setEnabled(!scanning&&gatt==null&&!preparing);sendProfile.setEnabled(ready&&idle);sendAvatar.setEnabled(ready&&idle);sendImage.setEnabled(ready&&idle&&fullscreenJpeg!=null);pickImage.setEnabled(idle);cancel.setEnabled(gatt!=null||scanning||preparing);}
    private boolean current(BluetoothGatt source){return !closed&&foreground&&source==gatt;}
    private void fail(String message){disconnect();if(!closed)state.setText(message);}
    private final BluetoothGattCallback callback=new BluetoothGattCallback(){
        @Override public void onConnectionStateChange(BluetoothGatt source,int status,int next){handler.post(()->{
            if(!current(source))return;
            if(status!=BluetoothGatt.GATT_SUCCESS||next==BluetoothProfile.STATE_DISCONNECTED){fail(transfer==null?"连接已断开，请唤醒设备后重试。":"连接中断，接收结果不确定。请检查设备后重新连接。");return;}
            if(next==BluetoothProfile.STATE_CONNECTED)try{state.setText("检查原版蓝牙服务…");if(!source.discoverServices())fail("无法发现服务，请重新连接。");}catch(SecurityException|IllegalStateException e){fail("蓝牙或权限已变更，请重新连接。");}
        });}
        @Override public void onServicesDiscovered(BluetoothGatt source,int status){handler.post(()->{
            if(!current(source))return;BluetoothGattService service=source.getService(StockProtocol.SERVICE);
            if(status!=BluetoothGatt.GATT_SUCCESS||service==null){fail("未发现原版 Passport 服务。");return;}
            command=service.getCharacteristic(StockProtocol.WRITE);response=service.getCharacteristic(StockProtocol.RESPONSE);
            if(command==null||response==null||(command.getProperties()&BluetoothGattCharacteristic.PROPERTY_WRITE)==0||(response.getProperties()&BluetoothGattCharacteristic.PROPERTY_NOTIFY)==0){fail("设备不支持所需的写入与回复通道。");return;}
            subscription=response.getDescriptor(CCC);if(subscription==null){fail("设备缺少回复订阅描述符。");return;}
            try{awaitingMtu=true;state.setText("协商蓝牙传输大小…");if(!source.requestMtu(247)){awaitingMtu=false;subscribe();}}
            catch(SecurityException|IllegalStateException e){fail("无法配置蓝牙连接，请重试。");}
        });}
        @Override public void onMtuChanged(BluetoothGatt source,int size,int status){handler.post(()->{
            if(!current(source))return;if(status==BluetoothGatt.GATT_SUCCESS&&size>=23&&size<=517)mtu=size;
            if(awaitingMtu){awaitingMtu=false;subscribe();}
        });}
        @Override public void onDescriptorWrite(BluetoothGatt source,BluetoothGattDescriptor descriptor,int status){handler.post(()->{
            if(!current(source)||!awaitingSubscription||descriptor!=subscription)return;
            awaitingSubscription=false;if(status!=BluetoothGatt.GATT_SUCCESS){fail("无法订阅设备回复，请重新连接。");return;}
            ready=true;generation++;state.setText("已连接，可以选择要发送的内容。尚未发送任何资料。");controls();
        });}
        @Override public void onCharacteristicWrite(BluetoothGatt source,BluetoothGattCharacteristic characteristic,int status){handler.post(()->{
            if(!current(source)||characteristic!=command||transfer==null||writeToken==0)return;
            long token=writeToken;writeToken=0;transfer.onWriteComplete(token,status==BluetoothGatt.GATT_SUCCESS,SystemClock.elapsedRealtime());pump();
        });}
        @Override public void onCharacteristicChanged(BluetoothGatt source,BluetoothGattCharacteristic characteristic){byte[] bytes=characteristic.getValue();notification(source,characteristic,bytes==null?null:bytes.clone());}
        @Override public void onCharacteristicChanged(BluetoothGatt source,BluetoothGattCharacteristic characteristic,byte[] value){notification(source,characteristic,value==null?null:value.clone());}
    };
    private void notification(BluetoothGatt source,BluetoothGattCharacteristic characteristic,byte[] value){handler.post(()->{
        if(!current(source)||characteristic!=response||transfer==null)return;
        transfer.onNotification(value,SystemClock.elapsedRealtime());pump();
    });}
    @SuppressWarnings("deprecation") private void subscribe(){
        try{if(gatt==null||!gatt.setCharacteristicNotification(response,true)){fail("无法启用设备回复。");return;}
            awaitingSubscription=true;state.setText("正在订阅设备回复…");boolean started;
            if(Build.VERSION.SDK_INT>=33)started=gatt.writeDescriptor(subscription,BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)==BluetoothStatusCodes.SUCCESS;
            else{subscription.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);started=gatt.writeDescriptor(subscription);}
            if(!started)fail("回复订阅请求未发送，请重新连接。");
        }catch(SecurityException|IllegalStateException e){fail("订阅失败，请检查蓝牙权限。");}
    }
    private void requestScan(){
        String[] permissions=Build.VERSION.SDK_INT>=31?new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT}:new String[]{Manifest.permission.ACCESS_FINE_LOCATION};
        for(String p:permissions)if(checkSelfPermission(p)!=PackageManager.PERMISSION_GRANTED){requestPermissions(permissions,42);return;}
        try{BluetoothManager manager=getSystemService(BluetoothManager.class);BluetoothAdapter adapter=manager==null?null:manager.getAdapter();
            if(adapter==null||!adapter.isEnabled()){state.setText("请先打开手机蓝牙。");return;}
            if(Build.VERSION.SDK_INT<31){LocationManager lm=getSystemService(LocationManager.class);if(lm==null||(!lm.isProviderEnabled(LocationManager.GPS_PROVIDER)&&!lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER))){state.setText("此 Android 版本搜索蓝牙需要开启位置信息。");return;}}
            disconnect();devices.clear();results.removeAllViews();scanner=adapter.getBluetoothLeScanner();if(scanner==null){state.setText("蓝牙搜索暂不可用。");return;}
            int attempt=++generation;scanning=true;scanCallback=new ScanCallback(){
                @Override public void onScanResult(int type,ScanResult r){handler.post(()->{if(attempt==generation)found(r);});}
                @Override public void onBatchScanResults(List<ScanResult> batch){handler.post(()->{if(attempt==generation)for(ScanResult r:batch)found(r);});}
                @Override public void onScanFailed(int error){handler.post(()->{if(closed||attempt!=generation||!scanning)return;generation++;stopScan();state.setText("搜索失败，请稍后重试。");});}
            };
            controls();state.setText("正在搜索，请保持设备唤醒…");scanner.startScan(null,new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),scanCallback);
            handler.postDelayed(()->{if(closed||attempt!=generation)return;stopScan();state.setText(devices.isEmpty()?"未找到设备，请唤醒 Passport 并断开微信连接。":"搜索完成，请选择设备。");},10000);
        }catch(SecurityException|IllegalStateException e){generation++;stopScan();state.setText("蓝牙已关闭或权限不足。");}
    }
    private void found(ScanResult result){if(closed||!foreground||!scanning)return;ScanRecord record=result.getScanRecord();String name=record==null?null:record.getDeviceName();
        boolean service=record!=null&&record.getServiceUuids()!=null&&record.getServiceUuids().contains(new ParcelUuid(StockProtocol.SERVICE));
        if(!service&&(name==null||!name.matches("(?i)[0-9a-f]{12}")))return;
        if(devices.put(result.getDevice().getAddress(),result.getDevice())!=null)return;
        results.addView(button(this,(name==null?"AI Passport":name)+" · "+result.getRssi()+" dBm",false,v->connect(result.getDevice())));
    }
    private void connect(BluetoothDevice device){try{disconnect();results.removeAllViews();state.setText("正在连接…");gatt=device.connectGatt(this,false,callback,BluetoothDevice.TRANSPORT_LE);controls();
        if(gatt==null){fail("无法发起连接。");return;}BluetoothGatt target=gatt;int attempt=++generation;
        handler.postDelayed(()->{if(!closed&&attempt==generation&&target==gatt&&!ready)fail("连接准备超时，请唤醒设备后重试。");},20000);
    }catch(SecurityException|IllegalStateException e){fail("连接失败，请检查蓝牙和权限。");}}
    private void stopScan(){scanning=false;if(scanner!=null){try{if(scanCallback!=null)scanner.stopScan(scanCallback);}catch(SecurityException|IllegalStateException ignored){}scanner=null;scanCallback=null;}controls();}
    private void disconnect(){generation++;preparation++;preparing=false;ready=false;awaitingMtu=false;awaitingSubscription=false;mtu=23;stopScan();
        if(transfer!=null)transfer.cancel();transfer=null;writeToken=0;BluetoothGatt old=gatt;gatt=null;command=null;response=null;subscription=null;
        if(old!=null){try{old.disconnect();}catch(SecurityException|IllegalStateException ignored){}try{old.close();}catch(SecurityException|IllegalStateException ignored){}}controls();}

    private void prepareProfile(){
        if(!ready||preparing||transfer!=null)return;
        try{Profile profile=new WalletRepository(this).load().profile();validateProfile(profile);
            List<StockProtocol.Command> commands=StockProtocol.profile(profile.nickname(),profile.bio());
            new AlertDialog.Builder(this).setTitle("发送昵称与简介？").setMessage(profile.nickname()+"\n\n"+profile.bio()+"\n\n会替换设备上的对应资料；社交账号与钱包不会发送。")
                .setPositiveButton("发送",(d,w)->startTransfer(commands,"昵称与简介")).setNegativeButton("取消",null).show();
        }catch(Exception e){message(e.getMessage());}
    }
    private void validateProfile(Profile profile){
        if(profile.nickname().getBytes(StandardCharsets.UTF_8).length>47)throw new IllegalArgumentException("昵称超过原版设备的 47 字节限制，请在名片页面缩短后重试（中文通常最多 15 字）。");
        if(profile.bio().codePointCount(0,profile.bio().length())>28||profile.bio().getBytes(StandardCharsets.UTF_8).length>84)throw new IllegalArgumentException("简介最多 28 个字符、84 个 UTF-8 字节，请在名片页面缩短后重试。");
    }
    private void prepareAvatar(){
        if(!ready||preparing||transfer!=null)return;int attempt=++preparation;preparing=true;controls();state.setText("正在准备 96 × 160 头像…");
        worker.execute(()->{try{
            String encoded=new WalletRepository(getApplicationContext()).load().profile().avatarBase64();if(encoded.isEmpty())throw new IOException("请先在名片页面选择头像。");
            byte[] bytes=Base64.decode(encoded,Base64.DEFAULT);if(bytes.length>512*1024)throw new IOException("头像文件过大。");
            Bitmap bitmap=StockImageEncoder.decode(()->new ByteArrayInputStream(bytes));byte[] jpeg;try{jpeg=StockImageEncoder.encode(bitmap,StockProtocol.ImageMode.AVATAR);}finally{bitmap.recycle();}
            handler.post(()->{if(closed||attempt!=preparation)return;preparing=false;controls();state.setText("头像已准备，请确认发送。");confirmImage(StockProtocol.ImageMode.AVATAR,jpeg);});
        }catch(Exception e){preparedError(attempt,e);}});
    }
    private void confirmImage(StockProtocol.ImageMode mode,byte[] jpeg){
        if(!ready||transfer!=null||preparing||jpeg==null)return;
        String label=mode==StockProtocol.ImageMode.AVATAR?"头像":"全屏图片";
        ImageView preview=new ImageView(this);Bitmap bitmap=BitmapFactory.decodeByteArray(jpeg,0,jpeg.length);preview.setImageBitmap(bitmap);preview.setAdjustViewBounds(true);preview.setMaxHeight(dp(this,280));
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("发送"+label+"？").setMessage("会替换设备上的"+label+"。设备接收后，请目视确认显示效果。")
            .setView(preview).setPositiveButton("发送",(d,w)->{try{startTransfer(StockProtocol.image(mode,jpeg,StockImageEncoder.IMAGE_MAX_BYTES),label);}catch(Exception e){message("图片无法发送，请重新选择。");}}).setNegativeButton("取消",null).create();
        dialog.setOnDismissListener(d->{preview.setImageDrawable(null);if(bitmap!=null)bitmap.recycle();});dialog.show();
    }
    private void startTransfer(List<StockProtocol.Command> commands,String label){
        if(!ready||transfer!=null||preparing){message("连接已变更，请重新连接后发送。");return;}
        StockTransferSession started=new StockTransferSession(commands,mtu,8000);
        transfer=started;transferLabel=label;controls();pump();if(transfer==started)tick(started);
    }
    private void tick(StockTransferSession expected){if(expected==null||closed||transfer!=expected)return;handler.postDelayed(()->{if(closed||transfer!=expected)return;expected.checkTimeout(SystemClock.elapsedRealtime());pump();if(transfer==expected)tick(expected);},200);}
    @SuppressWarnings("deprecation") private void pump(){
        if(transfer==null)return;
        if(transfer.terminal()){
            StockTransferSession finished=transfer;transfer=null;writeToken=0;
            if(finished.state()==StockTransferSession.State.TRANSPORT_COMPLETE){state.setText("设备已确认接收"+transferLabel+"。\n请查看设备确认显示；这不表示钱包同步或微信小程序兼容性已验证。");controls();}
            else{String detail=finished.state()==StockTransferSession.State.REJECTED?"设备拒绝传输（状态 "+String.format(Locale.ROOT,"%02X",finished.rejectedStatus())+"），已确认 "+finished.acknowledgedCommands()+" 项指令。":"传输未确认完成，设备可能已收到部分或全部内容。";fail(detail+"请检查设备后重新连接，系统不会自动重试。");}
            return;
        }
        state.setText("正在发送"+transferLabel+" · "+transfer.acknowledgedCommands()+" / "+transfer.totalCommands()+" 项指令已确认");
        if(writeToken!=0)return;StockTransferSession.Write write=transfer.nextWrite(SystemClock.elapsedRealtime());if(write==null)return;
        writeToken=write.token();boolean started=false;
        try{if(gatt==null||command==null)started=false;
            else if(Build.VERSION.SDK_INT>=33)started=gatt.writeCharacteristic(command,write.bytes(),BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)==BluetoothStatusCodes.SUCCESS;
            else{command.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);command.setValue(write.bytes());started=gatt.writeCharacteristic(command);}
        }catch(SecurityException|IllegalStateException ignored){}
        if(!started){writeToken=0;transfer.onWriteComplete(write.token(),false,SystemClock.elapsedRealtime());pump();}
    }
    @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(request!=PICK_IMAGE||result!=RESULT_OK||data==null||data.getData()==null)return;
        Uri uri=data.getData();int attempt=++preparation;preparing=true;controls();imageState.setText("正在裁剪并压缩图片…");
        worker.execute(()->{try{Bitmap source=StockImageEncoder.decode(()->getContentResolver().openInputStream(uri));byte[] jpeg;try{jpeg=StockImageEncoder.encode(source,StockProtocol.ImageMode.FULLSCREEN);}finally{source.recycle();}
            handler.post(()->{if(closed||attempt!=preparation)return;preparing=false;fullscreenJpeg=jpeg;imageState.setText("全屏图片已准备 · 240 × 320 · "+jpeg.length/1024+" KB。连接设备后点发送。");controls();});
        }catch(Exception e){preparedError(attempt,e);}});
    }
    private void preparedError(int attempt,Exception error){handler.post(()->{if(closed||attempt!=preparation)return;preparing=false;controls();state.setText("图片准备失败，请重新选择。");message(error instanceof IOException?error.getMessage():"图片无法处理，请重新选择。");});}
    private void message(String message){if(!closed&&!isFinishing())new AlertDialog.Builder(this).setTitle("暂时无法完成").setMessage(message==null?"请检查资料后重试。":message).setPositiveButton("知道了",null).show();}
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] grants){super.onRequestPermissionsResult(request,permissions,grants);if(request!=42||closed)return;boolean all=grants.length>0;for(int g:grants)all&=g==PackageManager.PERMISSION_GRANTED;if(all&&foreground)requestScan();else state.setText("需要附近设备权限才能连接。");}
    @Override protected void onStart(){super.onStart();foreground=true;}
    @Override protected void onStop(){foreground=false;boolean uncertain=transfer!=null;disconnect();if(!closed)state.setText(uncertain?"页面离开，传输已断开；接收结果不确定，请检查设备。":"已断开，可继续使用微信小程序。");super.onStop();}
    @Override protected void onDestroy(){closed=true;disconnect();handler.removeCallbacksAndMessages(null);worker.shutdownNow();fullscreenJpeg=null;super.onDestroy();}
}
