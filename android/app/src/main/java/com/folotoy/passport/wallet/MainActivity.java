package com.folotoy.passport.wallet;

import android.app.*;
import android.content.*;
import android.graphics.*;
import android.net.Uri;
import android.os.Bundle;
import android.util.Base64;
import android.view.Gravity;
import android.view.View;
import android.widget.*;
import com.folotoy.passport.wallet.core.*;
import com.folotoy.passport.wallet.data.WalletRepository;
import com.folotoy.passport.wallet.importer.*;
import com.folotoy.passport.wallet.provider.ProviderClient;
import com.folotoy.passport.wallet.render.QrMatrix;
import com.google.zxing.common.BitMatrix;
import java.io.*;
import java.math.RoundingMode;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.*;
import java.util.concurrent.*;
import static com.folotoy.passport.wallet.WalletUi.*;

/** Preview-first card, receiving wallet and honest device integration status. */
public final class MainActivity extends Activity {
    private static final int AVATAR=10, QR_GALLERY=11, IMPORT=13, EXPORT=14, MNEMONIC=15;
    private WalletRepository repo;
    private WalletBackup wallet;
    private Map<String,CachedAmount> cache;
    private LinearLayout shell,root,nav;
    private TextView status;
    private Button refreshButton;
    private ImageView profileImage;
    private final Set<ImageView> qrImages=new HashSet<>();
    private int destination;
    private boolean loadBlocked,refreshing,destroyed;
    private String notice="";
    private final ExecutorService worker=Executors.newSingleThreadExecutor();

    @Override public void onCreate(Bundle state) {
        setTheme(android.R.style.Theme_Material_NoActionBar);
        super.onCreate(state);
        getWindow().setStatusBarColor(BACKGROUND); getWindow().setNavigationBarColor(BACKGROUND);
        getWindow().getDecorView().setSystemUiVisibility(0);
        repo=new WalletRepository(this);
        if(state!=null)destination=state.getInt("destination",0);
        reload(); build();
        if(loadBlocked) new AlertDialog.Builder(this).setTitle("需要恢复资料")
            .setMessage("本地资料无法读取，原数据已保留。请导入有效备份后继续。")
            .setPositiveButton("导入备份",(d,w)->importBackup()).setNegativeButton("关闭",null).show();
        else if(state==null)handleShare(getIntent());
    }
    @Override protected void onResume() {
        super.onResume();
        if(repo!=null && root!=null) { reload(); render(); }
    }
    private void reload() {
        try { wallet=repo.load(); loadBlocked=false; }
        catch(Exception e) { loadBlocked=true;wallet=new WalletBackup(1,new Profile("","",""),List.of(),List.of(),List.of()); }
        cache=repo.cache();
    }
    @Override protected void onSaveInstanceState(Bundle out) { super.onSaveInstanceState(out);out.putInt("destination",destination); }

    private void build() {
        shell=column(this);shell.setBackgroundColor(BACKGROUND);shell.setFitsSystemWindows(true);
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setClipToPadding(false);
        root=column(this);root.setPadding(dp(24),dp(22),dp(24),dp(24));scroll.addView(root);
        shell.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        nav=row(this);nav.setPadding(dp(12),dp(8),dp(12),dp(12));nav.setBackground(shape(this,BACKGROUND,0,LINE));
        shell.addView(nav,new LinearLayout.LayoutParams(-1,-2));setContentView(shell);render();
    }
    private void render() {
        if(profileImage!=null){releaseImage(profileImage);profileImage=null;}
        root.removeAllViews();refreshButton=null;
        String[] titles={"我的名片","我的钱包","我的设备"};
        LinearLayout top=row(this);TextView title=text(titles[destination],28,true);
        top.addView(title,new LinearLayout.LayoutParams(0,-2,1));
        TextView mark=text(getString(R.string.app_name),10,true);mark.setTextColor(GREEN);mark.setLetterSpacing(.14f);top.addView(mark);
        root.addView(top);space(root,6);
        root.addView(muted(new String[]{"让每一次相遇，都留下你的样子。","收款与资产，一目了然。","预览名片，了解连接进度。"}[destination],13));space(root,24);
        if(loadBlocked) {panelMessage("资料已保护","请先导入有效备份，以恢复你的名片与钱包。");action(root,"导入备份",true,v->importBackup());}
        else if(destination==0)cardScreen(); else if(destination==1)walletScreen(); else deviceScreen();
        status=muted(notice,12);space(root,16);root.addView(status);
        nav.removeAllViews();String[] tabs={"名片","钱包","设备"};String[] icons={"▣","◇","▤"};
        for(int i=0;i<3;i++){final int index=i;TextView tab=text(icons[i]+"\n"+tabs[i],13,true);
            tab.setGravity(Gravity.CENTER);tab.setMinHeight(dp(56));tab.setPadding(0,dp(6),0,dp(6));
            tab.setTextColor(destination==i?GREEN:MUTED);tab.setContentDescription(tabs[i]);
            if(destination==i)tab.setBackground(shape(this,SURFACE,12,LINE));
            tab.setOnClickListener(v->{destination=index;notice="";render();});
            nav.addView(tab,new LinearLayout.LayoutParams(0,-2,1));}
    }
    private void cardScreen() {
        LinearLayout card=panel(root);card.setPadding(dp(24),dp(22),dp(24),dp(22));
        TextView label=muted("PERSONAL CARD",11);label.setLetterSpacing(.18f);card.addView(label);space(card,20);
        LinearLayout identity=row(this);identity.addView(avatarView(),new LinearLayout.LayoutParams(dp(72),dp(72)));
        LinearLayout words=column(this);words.setPadding(dp(18),0,0,0);
        words.addView(text(wallet.profile().nickname().isBlank()?"你的名字":wallet.profile().nickname(),26,true));
        space(words,5);TextView own=muted(getString(R.string.app_name)+" · 数字名片",12);words.addView(own);
        identity.addView(words,new LinearLayout.LayoutParams(0,-2,1));card.addView(identity);space(card,18);
        card.addView(text(wallet.profile().bio().isBlank()?"写一句介绍，让新朋友认识你。":wallet.profile().bio(),14,false));
        space(card,20);divider(card);space(card,12);
        if(wallet.socials().isEmpty())card.addView(muted("添加常用社交账号，展示在这里",12));
        else for(SocialLink social:wallet.socials()){
            LinearLayout line=row(this);line.setPadding(0,dp(6),0,dp(6));
            line.addView(muted(social.service(),12),new LinearLayout.LayoutParams(0,-2,1));
            TextView handle=text(social.handle(),13,true);handle.setMaxWidth(dp(190));line.addView(handle);card.addView(line);
        }
        space(root,16);LinearLayout actions=row(this);
        Button edit=button("编辑资料",true,v->profileDialog()),preview=button("设备预览",false,v->preview());
        actions.addView(edit,new LinearLayout.LayoutParams(0,-2,1));spaceHorizontal(actions,10);
        actions.addView(preview,new LinearLayout.LayoutParams(0,-2,1));root.addView(actions);
        section("名片内容");
        menuRow(root,"头像","选择照片并居中裁剪",()->pickImage(AVATAR));
        menuRow(root,"社交账号",wallet.socials().isEmpty()?"添加微信、X 或个人网站":wallet.socials().size()+" 个账号",this::socialList);
        section("随身收款");
        menuRow(root,"收款码与钱包",wallet.payments().size()+" 张收款码 · "+wallet.entries().size()+" 项资产",()->{destination=1;render();});
        space(root,16);root.addView(muted("资料保存在这台手机上，可在「设备」中同步到 Passport。",12));
    }
    private View avatarView() {
        if(!wallet.profile().avatarBase64().isEmpty())try{
            byte[] bytes=Base64.decode(wallet.profile().avatarBase64(),Base64.DEFAULT);
            BitmapFactory.Options o=new BitmapFactory.Options();o.inSampleSize=2;
            Bitmap b=BitmapFactory.decodeByteArray(bytes,0,bytes.length,o);
            if(b!=null){ImageView image=new ImageView(this);profileImage=image;image.setImageBitmap(b);image.setScaleType(ImageView.ScaleType.CENTER_CROP);
                image.setBackground(shape(this,LINE,18,0));image.setClipToOutline(true);image.setContentDescription("名片头像");return image;}
        }catch(Exception ignored){}
        TextView initial=text(wallet.profile().nickname().isBlank()?"我":wallet.profile().nickname().substring(0,wallet.profile().nickname().offsetByCodePoints(0,1)),28,true);
        initial.setGravity(Gravity.CENTER);initial.setTextColor(GREEN);initial.setBackground(shape(this,LINE,18,0));return initial;
    }
    private void walletScreen() {
        LinearLayout summary=panel(root);LinearLayout heading=row(this);
        heading.addView(muted("资产估值",13),new LinearLayout.LayoutParams(0,-2,1));
        Button currency=button(wallet.preferredCurrency()+" ▾",false,v->chooseCurrency());
        heading.addView(currency,new LinearLayout.LayoutParams(dp(88),dp(48)));summary.addView(heading);
        Valuation.Result value=Valuation.total(wallet.entries(),cache,cache,wallet.preferredCurrency(),Instant.now(),3600);
        TextView amount=text(value.includedEntries()==0?"—":money(value.total()),36,true);space(summary,8);summary.addView(amount);
        space(summary,6);summary.addView(muted(wallet.entries().isEmpty()?"导入钱包后，手动刷新余额与价格":value.incomplete()?"估值不完整 · 缺失或过期数据已标记":"按已导入资产计算 · 仅供参考",12));
        section("加密资产");
        if(wallet.entries().isEmpty())panelMessage("还没有导入钱包","使用助记词在本机派生账户，确认网络与地址后保存。这里只保留公开地址。");
        else for(WalletEntry e:wallet.entries())assetRow(e);
        action(root,"导入助记词",true,v->{if(ready())startActivityForResult(new Intent(this,MnemonicImportActivity.class),MNEMONIC);});
        if(!wallet.entries().isEmpty()) { refreshButton=button(refreshing?"正在刷新…":"刷新余额与价格",false,v->refreshNetwork());refreshButton.setEnabled(!refreshing);addSpaced(root,refreshButton,10); }
        section("收款码");
        if(wallet.payments().isEmpty())root.addView(muted("导入支付宝或微信的个人收款码图片。",13));
        for(PaymentCode payment:wallet.payments())menuRow(root,payment.provider().label(),"已导入 · 点击查看收款码",()->paymentDialog(payment));
        action(root,"导入收款码图片",false,v->pickImage(QR_GALLERY));
        space(root,12);root.addView(muted("余额按需查询。查询失败时保留上次成功值，并显示更新时间。",12));
    }
    private void assetRow(WalletEntry entry) {
        LinearLayout box=panel(root);LinearLayout row=row(this);LinearLayout identity=column(this);
        identity.addView(text(entry.asset(),18,true));space(identity,4);identity.addView(muted(entry.network().label(),12));
        row.addView(identity,new LinearLayout.LayoutParams(0,-2,.42f));spaceHorizontal(row,8);
        LinearLayout amounts=column(this);amounts.setGravity(Gravity.END);
        CachedAmount balance=cache.get(entry.key()),price=cache.get(entry.priceKey(wallet.preferredCurrency()));
        String quantity=balance==null||balance.missing()?"—":balance.value().stripTrailingZeros().toPlainString();
        String valuation=balance==null||balance.missing()||price==null||price.missing()?"暂无估值":money(balance.value().multiply(price.value()));
        amounts.addView(compactAmount(quantity,18),new LinearLayout.LayoutParams(-1,-2));
        TextView estimate=compactAmount(valuation,12);estimate.setTextColor(MUTED);amounts.addView(estimate,new LinearLayout.LayoutParams(-1,-2));
        row.addView(amounts,new LinearLayout.LayoutParams(0,-2,.58f));box.addView(row);
        space(box,10);String details="尚未刷新";
        if(balance!=null&&!balance.missing())details="余额 "+formatTime(balance.timestamp());
        if(price!=null&&!price.missing())details+=" · 价格 "+formatTime(price.timestamp());
        if((balance!=null&&balance.error()!=null)||(price!=null&&price.error()!=null))details+=" · 查询失败，保留上次数据";
        else if((balance!=null&&balance.stale(Instant.now(),3600))||(price!=null&&price.stale(Instant.now(),3600)))details+=" · 已过期";
        box.addView(muted(details,11));box.setOnClickListener(v->entryDetails(entry));box.setMinimumHeight(dp(88));
    }
    private void deviceScreen() {
        LinearLayout panel=panel(root);panel.setGravity(Gravity.CENTER_HORIZONTAL);
        TextView outline=text("AI\nPASSPORT",22,true);outline.setTextColor(GREEN);outline.setGravity(Gravity.CENTER);
        outline.setBackground(shape(this,BACKGROUND,24,LINE));LinearLayout.LayoutParams art=new LinearLayout.LayoutParams(dp(124),dp(156));art.setMargins(0,dp(8),0,dp(18));panel.addView(outline,art);
        panel.addView(text("尚未连接",22,true));space(panel,8);panel.addView(muted("原版微信小程序连接兼容性待验证",12));
        section("连接进度");
        panelMessage("连接并同步","新版固件可同步名片与钱包页面；原版固件支持昵称、简介、头像与图片。连接后会检查设备能力。微信小程序兼容性仍需实机验证。");
        action(root,"同步名片与钱包",true,v->{if(ready())startActivity(new Intent(this,BleSyncActivity.class));});
        action(root,"资料与图片传输",false,v->startActivity(new Intent(this,StockConnectActivity.class)));
        action(root,"查看设备页面预览",false,v->preview());
        space(root,8);root.addView(muted("预览只展示本地内容，不代表已连接或同步完成。",12));
        section("本地资料");
        menuRow(root,"导出备份","保存名片、收款码与公开地址",this::exportBackup);
        menuRow(root,"导入备份","用已保存的资料替换本地内容",this::confirmImport);
        space(root,14);root.addView(muted("备份含公开身份与财务地址，不含助记词或私钥。",12));
    }

    private void profileDialog() {
        LinearLayout content=dialogLayout();EditText name=field(content,"昵称",wallet.profile().nickname());
        EditText bio=field(content,"一句介绍",wallet.profile().bio());bio.setMinLines(3);bio.setGravity(Gravity.TOP);
        name.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(80)});
        bio.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(800)});
        saveDialog("编辑资料",content,()->save(new WalletBackup(1,new Profile(name.getText().toString().trim(),bio.getText().toString().trim(),wallet.profile().avatarBase64()),wallet.socials(),wallet.payments(),wallet.entries(),wallet.preferredCurrency())));
    }
    private void socialList() {
        LinearLayout content=dialogLayout();
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("社交账号").setView(scroll(content)).setNegativeButton("完成",null).create();
        for(SocialLink social:wallet.socials())menuRow(content,social.service(),social.handle(),()->{dialog.dismiss();socialDialog(social);});
        action(content,"添加账号",true,v->{dialog.dismiss();socialDialog(null);});dialog.show();
    }
    private void socialDialog(SocialLink old) {
        LinearLayout content=dialogLayout();EditText service=field(content,"平台，例如微信、X、网站",old==null?"":old.service());
        EditText handle=field(content,"账号",old==null?"":old.handle());EditText url=field(content,"公开链接（可选）",old==null?"":old.url());
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle(old==null?"添加社交账号":"编辑社交账号").setView(scroll(content))
            .setPositiveButton("保存",null).setNegativeButton("取消",null).setNeutralButton(old==null?null:"移除",(d,w)->removeSocial(old)).create();
        dialog.setOnShowListener(d->dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{try{
            if(service.getText().toString().isBlank()||handle.getText().toString().isBlank())throw new IllegalArgumentException("请填写平台与账号");
            SocialLink next=new SocialLink(service.getText().toString().trim(),handle.getText().toString().trim(),url.getText().toString().trim());
            List<SocialLink> list=new ArrayList<>(wallet.socials());replace(list,old,next);
            save(new WalletBackup(1,wallet.profile(),list,wallet.payments(),wallet.entries(),wallet.preferredCurrency()));dialog.dismiss();
        }catch(Exception e){error(e.getMessage());}}));dialog.show();
    }
    private void chooseCurrency() {
        new AlertDialog.Builder(this).setTitle("估值货币").setSingleChoiceItems(new String[]{"USD · 美元","CNY · 人民币"},"USD".equals(wallet.preferredCurrency())?0:1,(d,w)->{
            runSafe(()->save(new WalletBackup(1,wallet.profile(),wallet.socials(),wallet.payments(),wallet.entries(),w==0?"USD":"CNY")));d.dismiss();}).setNegativeButton("取消",null).show();
    }
    private void entryDetails(WalletEntry entry) {
        LinearLayout content=dialogLayout();content.addView(text(entry.network().label()+" · "+entry.asset(),18,true));space(content,12);
        final ImageView code;
        try{code=addQrImage(content,entry.address(),entry.network().label()+"收款地址二维码");}
        catch(Exception e){error("无法生成地址二维码");return;}
        space(content,12);TextView address=text(entry.address(),14,false);address.setTextIsSelectable(true);content.addView(address);space(content,12);
        AssetRegistry.Asset asset=AssetRegistry.get(entry.network(),entry.asset());content.addView(muted(asset.origin(),12));
        CachedAmount balance=cache.get(entry.key()),price=cache.get(entry.priceKey(wallet.preferredCurrency()));
        if(balance!=null&&!balance.missing()){
            space(content,16);content.addView(muted("完整余额 · "+formatTime(balance.timestamp()),12));
            TextView quantity=text(balance.value().stripTrailingZeros().toPlainString()+" "+entry.asset(),18,true);
            quantity.setTextIsSelectable(true);content.addView(quantity);
            if(price!=null&&!price.missing()){
                space(content,12);content.addView(muted("估值 · "+wallet.preferredCurrency()+" · 价格 "+formatTime(price.timestamp()),12));
                TextView valuation=text(money(balance.value().multiply(price.value())),18,true);valuation.setTextIsSelectable(true);content.addView(valuation);
            }
        }
        for(String key:List.of(entry.key(),entry.priceKey(wallet.preferredCurrency()))) {CachedAmount amount=cache.get(key);if(amount!=null&&amount.error()!=null){space(content,8);content.addView(muted(amount.error(),12));}}
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("公开收款地址").setView(scroll(content)).setPositiveButton("完成",null)
            .setNeutralButton("移除",(d,w)->confirmRemove(()->{List<WalletEntry> list=new ArrayList<>(wallet.entries());list.remove(entry);save(new WalletBackup(1,wallet.profile(),wallet.socials(),wallet.payments(),list,wallet.preferredCurrency()));})).create();
        dialog.setOnDismissListener(d->releaseImage(code));dialog.show();
    }
    private void paymentDialog(PaymentCode payment) {
        showPayment(payment,false);
    }
    private void showPayment(PaymentCode payment,boolean imported) {
        LinearLayout content=dialogLayout();content.setGravity(Gravity.CENTER_HORIZONTAL);
        final ImageView code;
        try{code=addQrImage(content,payment.payload(),payment.provider().label()+"收款码");}
        catch(Exception e){error("收款码内容过长，无法显示");return;}
        space(content,16);content.addView(muted(imported?"请确认这是你的个人收款码。保留二维码原始内容。":"来自导入的收款码图片，保留二维码原始内容。",13));
        AlertDialog.Builder dialog=new AlertDialog.Builder(this).setTitle(payment.provider().label()).setView(scroll(content)).setNegativeButton(imported?"取消":"关闭",null);
        if(imported)dialog.setPositiveButton("确认导入",(d,w)->runSafe(()->{
            List<PaymentCode> list=new ArrayList<>(wallet.payments());list.removeIf(p->p.provider()==payment.provider());list.add(payment);
            save(new WalletBackup(1,wallet.profile(),wallet.socials(),list,wallet.entries(),wallet.preferredCurrency()));
        }));
        else dialog.setNeutralButton("移除",(d,w)->confirmRemove(()->{List<PaymentCode> list=new ArrayList<>(wallet.payments());list.remove(payment);save(new WalletBackup(1,wallet.profile(),wallet.socials(),list,wallet.entries(),wallet.preferredCurrency()));}));
        AlertDialog shown=dialog.create();shown.setOnDismissListener(d->releaseImage(code));shown.show();
    }
    private ImageView addQrImage(LinearLayout content,String payload,String description)throws Exception {
        BitMatrix qr=QrMatrix.encode(payload);int scale=Math.max(1,720/qr.getWidth()),size=qr.getWidth()*scale;
        Bitmap bitmap=Bitmap.createBitmap(size,size,Bitmap.Config.ARGB_8888);int[] pixels=new int[size*size];
        for(int y=0;y<size;y++)for(int x=0;x<size;x++)pixels[y*size+x]=qr.get(x/scale,y/scale)?Color.BLACK:Color.WHITE;
        bitmap.setPixels(pixels,0,size,0,0,size,size);ImageView image=new ImageView(this);image.setImageBitmap(bitmap);
        image.setScaleType(ImageView.ScaleType.FIT_CENTER);image.setContentDescription(description);qrImages.add(image);
        int side=Math.min(dp(240),getResources().getDisplayMetrics().widthPixels-dp(112));
        LinearLayout.LayoutParams params=new LinearLayout.LayoutParams(side,side);params.gravity=Gravity.CENTER_HORIZONTAL;content.addView(image,params);return image;
    }
    private void releaseImage(ImageView image) {
        android.graphics.drawable.Drawable drawable=image.getDrawable();image.setImageDrawable(null);qrImages.remove(image);
        if(drawable instanceof android.graphics.drawable.BitmapDrawable){Bitmap bitmap=((android.graphics.drawable.BitmapDrawable)drawable).getBitmap();if(!bitmap.isRecycled())bitmap.recycle();}
    }
    private TextView compactAmount(String value,int size) {
        TextView amount=text(value,size,true);amount.setMaxLines(1);amount.setEllipsize(android.text.TextUtils.TruncateAt.END);
        amount.setAutoSizeTextTypeUniformWithConfiguration(10,size,1,android.util.TypedValue.COMPLEX_UNIT_SP);
        amount.setGravity(Gravity.END);amount.setContentDescription(value+"，点击资产查看完整数值");return amount;
    }
    private void acceptQr(String payload) {runSafe(()->{PaymentCode.Provider p=PaymentCode.detectProvider(payload);showPayment(new PaymentCode(p.label(),payload),true);});}
    private void removeSocial(SocialLink social) {confirmRemove(()->{List<SocialLink> list=new ArrayList<>(wallet.socials());list.remove(social);save(new WalletBackup(1,wallet.profile(),list,wallet.payments(),wallet.entries(),wallet.preferredCurrency()));});}
    private void confirmRemove(Runnable action) {new AlertDialog.Builder(this).setTitle("移除此内容？").setPositiveButton("移除",(d,w)->runSafe(action)).setNegativeButton("取消",null).show();}
    private void save(WalletBackup next) {if(!ready())throw new IllegalStateException("请先恢复备份");if(!repo.compareAndSave(wallet,next)){reload();render();throw new IllegalStateException("资料已在另一个页面更新，请重新操作；新导入的地址已保留");}wallet=next;render();}
    private boolean ready() {if(loadBlocked){error("请先导入有效备份，本地原数据已保留");return false;}return true;}
    private void preview() {if(ready())startActivity(new Intent(this,PreviewActivity.class));}

    private void pickImage(int request) {if(ready())startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("image/*").addCategory(Intent.CATEGORY_OPENABLE),request);}
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);if(result!=RESULT_OK)return;
        if(request==MNEMONIC){reload();destination=1;render();return;}
        Uri uri=data==null?null:data.getData();if(uri==null){error("无法读取所选文件");return;}
        if(request==AVATAR||request==QR_GALLERY)processImage(request,uri);else if(request==IMPORT)processImport(uri);else if(request==EXPORT)processExport(uri);
    }
    private void processImage(int request,Uri uri) {
        updateNotice(request==AVATAR?"正在处理头像…":"正在读取收款码…");
        submit(()->{Bitmap source=decodeBounded(uri,request==AVATAR?1024:2048);try{
            if(request==AVATAR){Bitmap square=AvatarCropper.centerSquare(source,256);try(ByteArrayOutputStream out=new ByteArrayOutputStream()){
                if(!square.compress(Bitmap.CompressFormat.PNG,100,out))throw new IOException("无法保存头像");String avatar=Base64.encodeToString(out.toByteArray(),Base64.NO_WRAP);
                post(()->runSafe(()->{save(new WalletBackup(1,new Profile(wallet.profile().nickname(),wallet.profile().bio(),avatar),wallet.socials(),wallet.payments(),wallet.entries(),wallet.preferredCurrency()));updateNotice("头像已保存");}));
            }finally{square.recycle();}}
            else{String payload=QrImageDecoder.decode(source);QrImageDecoder.requireReusableReceivePayload(payload);post(()->{updateNotice("");acceptQr(payload);});}
        }finally{source.recycle();}},"图片处理失败");
    }
    private void confirmImport() {new AlertDialog.Builder(this).setTitle("导入备份").setMessage("导入会替换当前名片、收款码与公开地址。建议先导出当前资料。")
        .setPositiveButton("选择备份",(d,w)->importBackup()).setNegativeButton("取消",null).show();}
    private void importBackup() {startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("application/json").addCategory(Intent.CATEGORY_OPENABLE),IMPORT);}
    private void exportBackup() {if(ready())startActivityForResult(new Intent(Intent.ACTION_CREATE_DOCUMENT).setType("application/json").putExtra(Intent.EXTRA_TITLE,"passport-card-wallet-backup.json"),EXPORT);}
    private void processImport(Uri uri) {
        updateNotice("正在导入备份…");submit(()->{WalletBackup imported;try(InputStream in=open(uri)){imported=BackupCodec.decode(new String(readAll(in),StandardCharsets.UTF_8));}
            AvatarCropper.validateEncoded(imported.profile().avatarBase64());repo.save(imported);
            post(()->{wallet=imported;loadBlocked=false;cache=repo.cache();notice="备份已导入";render();});},"备份导入失败");
    }
    private void processExport(Uri uri) {if(!ready())return;WalletBackup target=wallet;updateNotice("正在导出备份…");
        submit(()->{try(OutputStream out=getContentResolver().openOutputStream(uri,"wt")){if(out==null)throw new IOException("无法打开目标文件");out.write(BackupCodec.encode(target).getBytes(StandardCharsets.UTF_8));}post(()->updateNotice("备份已导出"));},"备份导出失败");}
    private Bitmap decodeBounded(Uri uri,int max)throws IOException {
        BitmapFactory.Options bounds=new BitmapFactory.Options();bounds.inJustDecodeBounds=true;try(InputStream in=open(uri)){BitmapFactory.decodeStream(in,null,bounds);}
        if(bounds.outWidth<1||bounds.outHeight<1||bounds.outWidth>32768||bounds.outHeight>32768)throw new IOException("图片无效或过大");int sample=1;
        while(bounds.outWidth/sample>max||bounds.outHeight/sample>max||(long)(bounds.outWidth/sample)*(bounds.outHeight/sample)>4_194_304L)sample*=2;
        BitmapFactory.Options options=new BitmapFactory.Options();options.inSampleSize=sample;options.inPreferredConfig=Bitmap.Config.ARGB_8888;
        try(InputStream in=open(uri)){Bitmap b=BitmapFactory.decodeStream(in,null,options);if(b==null)throw new IOException("无法解码图片");return b;}
    }
    private InputStream open(Uri uri)throws IOException {InputStream in=getContentResolver().openInputStream(uri);if(in==null)throw new IOException("无法读取文件");return in;}
    private void handleShare(Intent intent) {
        if(intent==null||!Intent.ACTION_SEND.equals(intent.getAction()))return;
        if(intent.getType()!=null&&intent.getType().startsWith("image/")){Uri uri=intent.getParcelableExtra(Intent.EXTRA_STREAM);if(uri!=null)processImage(QR_GALLERY,uri);}
        else error("请分享支付宝或微信的收款码图片");
    }

    private void refreshNetwork() {
        if(refreshing||!ready())return;refreshing=true;render();updateNotice("正在查询余额与价格…");
        WalletBackup target=wallet;Map<String,CachedAmount> base=new HashMap<>(cache);
        worker.execute(()->{try{
            Map<String,CachedAmount> next=new HashMap<>(base);
            for(WalletEntry e:target.entries())try{next.put(e.key(),CachedAmount.success(ProviderClient.balance(e),Instant.now()));}
                catch(Exception ex){retainFailure(next,e.key(),cleanError(ex));}
            try{
                Map<String,ProviderClient.PriceQuote> prices=ProviderClient.prices(target.entries());
                for(WalletEntry e:target.entries())for(String currency:List.of("USD","CNY")){
                    String key=e.priceKey(currency);ProviderClient.PriceQuote quote=prices.get(key);
                    if(quote!=null)next.put(key,CachedAmount.success(quote.value(),quote.providerTimestamp()));
                    else retainFailure(next,key,"价格缺失或无效");
                }
            }catch(Exception ex){for(WalletEntry e:target.entries())for(String currency:List.of("USD","CNY"))retainFailure(next,e.priceKey(currency),cleanError(ex));}
            String failure=null;try{repo.saveCache(next);}catch(Exception e){failure=cleanError(e);}
            String persistenceFailure=failure;
            post(()->{refreshing=false;cache=next;notice=persistenceFailure==null?"刷新完成 · 每项资产显示各自的数据状态":"查询完成，但缓存保存失败："+persistenceFailure;render();});
        }catch(Exception e){post(()->{refreshing=false;render();error("刷新失败："+cleanError(e));});}});
    }
    private static void retainFailure(Map<String,CachedAmount> values,String key,String error) {
        CachedAmount previous=values.get(key);values.put(key,previous==null?new CachedAmount(null,null,error):previous.failureKeepingValue(error));
    }
    private String money(java.math.BigDecimal value) {return ("CNY".equals(wallet.preferredCurrency())?"¥":"$")+value.setScale(2,RoundingMode.HALF_UP).toPlainString();}
    private String formatTime(Instant time) {return DateTimeFormatter.ofPattern("MM-dd HH:mm").withZone(ZoneId.systemDefault()).format(time);}
    private int dp(int n) {return WalletUi.dp(this,n);}
    private TextView text(String s,int size,boolean bold) {return WalletUi.text(this,s,size,bold);}
    private TextView muted(String s,int size) {TextView v=text(s,size,false);v.setTextColor(MUTED);return v;}
    private Button button(String s,boolean primary,View.OnClickListener action) {return WalletUi.button(this,s,primary,action);}
    private void section(String label) {space(root,26);root.addView(text(label,15,true));space(root,10);}
    private void space(LinearLayout parent,int n) {View space=new View(this);parent.addView(space,new LinearLayout.LayoutParams(1,dp(n)));}
    private void spaceHorizontal(LinearLayout parent,int n) {View space=new View(this);parent.addView(space,new LinearLayout.LayoutParams(dp(n),1));}
    private void divider(LinearLayout parent) {View v=new View(this);v.setBackgroundColor(LINE);parent.addView(v,new LinearLayout.LayoutParams(-1,dp(1)));}
    private LinearLayout panel(LinearLayout parent) {LinearLayout box=column(this);box.setPadding(dp(18),dp(16),dp(18),dp(16));box.setBackground(shape(this,SURFACE,18,LINE));addSpaced(parent,box,8);return box;}
    private void panelMessage(String title,String body) {LinearLayout box=panel(root);box.addView(text(title,17,true));space(box,8);box.addView(muted(body,13));}
    private void addSpaced(LinearLayout parent,View view,int top) {LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);lp.topMargin=dp(top);parent.addView(view,lp);}
    private void action(LinearLayout parent,String title,boolean primary,View.OnClickListener action) {addSpaced(parent,button(title,primary,action),14);}
    private void menuRow(LinearLayout parent,String title,String subtitle,Runnable action) {
        LinearLayout line=row(this);line.setMinimumHeight(dp(72));line.setPadding(0,dp(12),0,dp(12));
        LinearLayout copy=column(this);copy.addView(text(title,15,true));space(copy,4);copy.addView(muted(subtitle,12));
        line.addView(copy,new LinearLayout.LayoutParams(0,-2,1));TextView arrow=text("›",24,false);arrow.setTextColor(MUTED);line.addView(arrow);
        line.setOnClickListener(v->action.run());line.setFocusable(true);line.setContentDescription(title+"，"+subtitle);parent.addView(line);divider(parent);
    }
    private LinearLayout dialogLayout() {LinearLayout c=column(this);c.setPadding(dp(24),dp(12),dp(24),dp(20));return c;}
    private ScrollView scroll(LinearLayout content) {ScrollView s=new ScrollView(this);s.addView(content);return s;}
    private EditText field(LinearLayout parent,String label,String value) {parent.addView(muted(label,12));EditText e=new EditText(this);e.setText(value);e.setTextColor(INK);e.setTextSize(16);e.setMinHeight(dp(52));e.setBackgroundTintList(android.content.res.ColorStateList.valueOf(GREEN));parent.addView(e);space(parent,14);return e;}
    private void saveDialog(String title,LinearLayout content,Runnable save) {
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle(title).setView(scroll(content)).setPositiveButton("保存",null).setNegativeButton("取消",null).create();
        dialog.setOnShowListener(d->dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{try{save.run();dialog.dismiss();}catch(Exception e){error(e.getMessage());}}));dialog.show();
    }
    private static <T> void replace(List<T> list,T old,T value) {if(old==null)list.add(value);else{int index=list.indexOf(old);if(index<0)throw new IllegalStateException("内容已变更，请重新打开");list.set(index,value);}}
    private void updateNotice(String value) {notice=value;if(status!=null)status.setText(value);}
    private String cleanError(Exception e) {String s=e.getMessage();return s==null?e.getClass().getSimpleName():s.substring(0,Math.min(120,s.length())).replace('|','/').replace('\n',' ');}
    private static byte[] readAll(InputStream in)throws IOException {ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] buffer=new byte[8192];int size=0;for(int n;(n=in.read(buffer))>=0;){size+=n;if(size>2_000_000)throw new IOException("备份超过 2 MB");out.write(buffer,0,n);}return out.toByteArray();}
    private interface Work {void run()throws Exception;}
    private void submit(Work work,String prefix) {worker.execute(()->{try{work.run();}catch(Exception e){post(()->{updateNotice("");error(prefix+"："+cleanError(e));});}});}
    private void post(Runnable action) {runOnUiThread(()->{if(!destroyed&&!isFinishing())action.run();});}
    private void runSafe(Runnable action) {try{action.run();}catch(Exception e){error(e.getMessage());}}
    private void error(String message) {if(!destroyed&&!isFinishing())new AlertDialog.Builder(this).setTitle("暂时无法完成").setMessage(message==null?"请稍后重试":message).setPositiveButton("知道了",null).show();}
    @Override protected void onDestroy() {destroyed=true;worker.shutdownNow();if(profileImage!=null)releaseImage(profileImage);for(ImageView image:new ArrayList<>(qrImages))releaseImage(image);super.onDestroy();}
}
