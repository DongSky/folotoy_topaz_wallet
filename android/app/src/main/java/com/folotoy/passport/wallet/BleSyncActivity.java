package com.folotoy.passport.wallet;

import android.Manifest;
import android.app.*;
import android.bluetooth.BluetoothDevice;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.widget.*;
import com.folotoy.passport.wallet.ble.WalletBleClient;
import com.folotoy.passport.wallet.core.Pcw1;
import com.folotoy.passport.wallet.data.WalletRepository;
import com.folotoy.passport.wallet.render.DevicePageRenderer;
import java.time.Instant;
import java.util.List;
import java.util.concurrent.*;
import static com.folotoy.passport.wallet.WalletUi.*;

/** Public rendered pages only. Mnemonic/seed never enters snapshot or BLE. */
public final class BleSyncActivity extends Activity implements WalletBleClient.Listener {
    private static final int PERMS=88;
    private TextView status;private Button scan,sync,cancel;private WalletBleClient client;
    private byte[] snapshot;private int pages;private boolean foreground;private volatile boolean destroyed;
    private WalletBleClient.State connection=WalletBleClient.State.IDLE;
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    @Override public void onCreate(Bundle saved){setTheme(android.R.style.Theme_Material_NoActionBar);super.onCreate(saved);
        getWindow().setStatusBarColor(BACKGROUND);getWindow().setNavigationBarColor(BACKGROUND);
        ScrollView scroll=new ScrollView(this);scroll.setFitsSystemWindows(true);scroll.setBackgroundColor(BACKGROUND);
        LinearLayout content=column(this);content.setPadding(dp(this,24),dp(this,24),dp(this,24),dp(this,28));scroll.addView(content);setContentView(scroll);
        add(content,button(this,"‹ 返回",false,v->finish()));add(content,text(this,"同步名片与钱包",26,true));
        add(content,copy("需要支持钱包同步的新版固件。原版固件会在配对前识别并提示，不会向其发送钱包包。"));
        add(content,copy("1. 在设备长按上键，开启两分钟同步窗口。\n2. 搜索并选择设备，首次配对时在设备按 OK。\n3. 在 Android 配对提示中输入设备显示的六位码。\n4. 安全连接就绪后，点击同步页面。"));
        add(content,copy("更换同步手机：在设备长按下键，再按 OK 确认解除旧配对。已授权手机的安全连接不受窗口到期影响。"));
        scan=button(this,"搜索设备",true,v->scan());add(content,scan);
        sync=button(this,"同步页面",true,v->confirmSync());add(content,sync);
        cancel=button(this,"断开连接",false,v->{client.close();connection=WalletBleClient.State.IDLE;status.setText("已断开。若刚才正在提交，请先查看设备确认结果。");controls();});add(content,cancel);
        status=copy("正在生成公开页面…");add(content,status);
        add(content,copy("同步内容包括名片、社交账号、收款码、公开收款地址和资产显示页面；不含助记词或私钥。设备主页昵称、简介与头像请通过“资料与图片传输”另行发送。"));
        client=new WalletBleClient(this,this);controls();
        worker.execute(()->{DevicePageRenderer.Rendered rendered=null;try{
            WalletRepository repository=new WalletRepository(getApplicationContext());var cache=repository.cache();
            rendered=DevicePageRenderer.render(repository.load(),cache,cache);byte[] bytes=Pcw1.encode(rendered.pages(),Instant.now().getEpochSecond());int count=rendered.pages().size();
            runOnUiThread(()->{if(destroyed)return;snapshot=bytes;pages=count;if(connection==WalletBleClient.State.IDLE)status.setText("页面已准备 · "+pages+" / 52 页 · "+bytes.length/1024+" KB");controls();});
        }catch(Exception e){runOnUiThread(()->{if(!destroyed){status.setText("无法生成页面："+e.getMessage());controls();}});}
        finally{if(rendered!=null)for(var bitmap:rendered.previews())bitmap.recycle();}});
    }
    private TextView copy(String value){TextView view=text(this,value,14,false);view.setTextColor(MUTED);return view;}
    private void add(LinearLayout parent,android.view.View view){LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(-1,-2);params.bottomMargin=dp(this,16);parent.addView(view,params);}
    private void controls(){if(scan==null)return;boolean idle=connection==WalletBleClient.State.IDLE||connection==WalletBleClient.State.ERROR||connection==WalletBleClient.State.COMPLETE;
        scan.setEnabled(snapshot!=null&&foreground&&idle);sync.setEnabled(snapshot!=null&&foreground&&connection==WalletBleClient.State.READY);cancel.setEnabled(!idle);}
    private void scan(){if(!foreground||snapshot==null)return;
        String[] permissions=android.os.Build.VERSION.SDK_INT>=31?new String[]{Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT}:new String[]{Manifest.permission.ACCESS_FINE_LOCATION};
        for(String permission:permissions)if(checkSelfPermission(permission)!=PackageManager.PERMISSION_GRANTED){requestPermissions(permissions,PERMS);return;}client.scan();}
    private void confirmSync(){if(connection!=WalletBleClient.State.READY)return;new AlertDialog.Builder(this).setTitle("同步 "+pages+" 页到设备？")
        .setMessage("会替换设备当前的钱包页面。请保持应用前台并靠近设备；只有设备确认校验与提交后才显示完成。")
        .setPositiveButton("开始同步",(d,w)->{if(foreground&&connection==WalletBleClient.State.READY)client.transfer(snapshot);}).setNegativeButton("取消",null).show();}
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] grants){super.onRequestPermissionsResult(request,permissions,grants);if(request!=PERMS||destroyed)return;
        boolean granted=grants.length>0;for(int value:grants)granted&=value==PackageManager.PERMISSION_GRANTED;if(granted&&foreground)scan();else error("需要附近设备权限才能同步。");}
    @Override public void state(WalletBleClient.State next,String message){if(destroyed||!foreground)return;connection=next;status.setText(message);controls();}
    @Override public void devices(List<BluetoothDevice> devices){if(destroyed||!foreground)return;String[] labels=new String[devices.size()];for(int i=0;i<labels.length;i++)labels[i]=label(devices.get(i));
        new AlertDialog.Builder(this).setTitle("选择 Passport").setItems(labels,(dialog,index)->{if(foreground)client.connectSelected(devices.get(index));})
            .setNegativeButton("取消",(dialog,which)->{client.close();connection=WalletBleClient.State.IDLE;controls();}).setOnCancelListener(dialog->{client.close();connection=WalletBleClient.State.IDLE;controls();}).show();}
    private String label(BluetoothDevice device){try{String name=device.getName();return (name==null?"Passport":name)+" · "+device.getAddress();}catch(SecurityException e){return "Passport";}}
    @Override public void complete(){if(destroyed||!foreground)return;new AlertDialog.Builder(this).setTitle("设备已完成同步")
        .setMessage("设备已校验并提交 "+pages+" 页。请打开设备的名片入口查看内容；这不代替实机显示与二维码扫码检查。")
        .setPositiveButton("完成",(dialog,which)->finish()).show();}
    @Override public void error(String message){if(destroyed||!foreground)return;connection=WalletBleClient.State.ERROR;status.setText(message);controls();}
    @Override protected void onStart(){super.onStart();foreground=true;controls();}
    @Override protected void onStop(){foreground=false;if(client!=null)client.close();if(connection!=WalletBleClient.State.COMPLETE){status.setText(connection==WalletBleClient.State.SENDING?"已离开同步页面，结果需在设备确认；请重新连接。":"连接已断开，返回后请重新搜索。");connection=WalletBleClient.State.IDLE;}controls();super.onStop();}
    @Override protected void onDestroy(){destroyed=true;worker.shutdownNow();if(client!=null)client.close();snapshot=null;super.onDestroy();}
}
