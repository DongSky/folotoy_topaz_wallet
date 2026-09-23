package com.folotoy.passport.wallet;

import static org.junit.Assert.*;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.util.Base64;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import com.folotoy.passport.wallet.core.*;
import com.folotoy.passport.wallet.importer.QrImageDecoder;
import com.folotoy.passport.wallet.render.DevicePageRenderer;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.math.BigDecimal;
import java.time.Instant;
import java.util.*;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class DeviceFixtureTest {
    @Test public void exportsActualAndroidPcwAndQuantizedPngs() throws Exception {
        Bitmap avatar=Bitmap.createBitmap(256,256,Bitmap.Config.ARGB_8888);Canvas canvas=new Canvas(avatar);canvas.drawColor(Color.rgb(21,93,255));android.graphics.Paint p=new android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG);p.setColor(Color.rgb(255,200,87));canvas.drawCircle(128,105,54,p);p.setColor(Color.rgb(42,157,143));canvas.drawRect(50,160,206,250,p);ByteArrayOutputStream avatarBytes=new ByteArrayOutputStream();avatar.compress(Bitmap.CompressFormat.PNG,100,avatarBytes);
        String eth="0x52908400098527886E0F7030069857D2E4169EE7",btc="bc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",sol="11111111111111111111111111111111",social="https://example.com/你好",payment="https://qr.alipay.com/fkx1234567890example";
        WalletEntry e1=new WalletEntry(Network.ETHEREUM,eth,"ETH","Main ETH"),e2=new WalletEntry(Network.BITCOIN,btc,"BTC","Savings BTC"),e3=new WalletEntry(Network.SOLANA,sol,"SOL","Public SOL");
        WalletBackup wallet=new WalletBackup(1,new Profile("陈小卡 Alex","Synthetic offline card for device visual review. This is not a real identity.",Base64.encodeToString(avatarBytes.toByteArray(),Base64.NO_WRAP)),List.of(new SocialLink("Web","@alex_very_long_public_handle",social)),List.of(new PaymentCode("支付宝",payment)),List.of(e1,e2,e3),"USD");
        Instant now=Instant.now();Map<String,CachedAmount> cache=new HashMap<>();cache.put(e1.key(),CachedAmount.success(new BigDecimal("1.25"),now));cache.put(e2.key(),CachedAmount.success(new BigDecimal("0.01"),now));cache.put(e3.key(),CachedAmount.success(new BigDecimal("4.5"),now));cache.put(e1.priceKey("USD"),CachedAmount.success(new BigDecimal("3200.50"),now));cache.put(e2.priceKey("USD"),CachedAmount.success(new BigDecimal("65000.25"),now));cache.put(e3.priceKey("USD"),CachedAmount.success(new BigDecimal("145.75"),now));cache.put(e1.priceKey("CNY"),CachedAmount.success(new BigDecimal("23000.10"),now));cache.put(e2.priceKey("CNY"),CachedAmount.success(new BigDecimal("470000.20"),now));cache.put(e3.priceKey("CNY"),CachedAmount.success(new BigDecimal("1050.30"),now));
        DevicePageRenderer.Rendered rendered=DevicePageRenderer.render(wallet,cache,cache);byte[] pcw=Pcw1.encode(rendered.pages(),now.getEpochSecond());assertTrue(rendered.pages().size()>8);assertEquals(32+27716*rendered.pages().size(),pcw.length);
        Set<String> decoded=new HashSet<>();for(Bitmap bitmap:rendered.previews())try{decoded.add(QrImageDecoder.decode(bitmap));}catch(Exception ignored){}assertTrue(decoded.containsAll(Set.of(social,payment,eth,btc,sol)));
        Context context=InstrumentationRegistry.getInstrumentation().getTargetContext();File dir=new File(context.getExternalFilesDir(null),"fixture");assertTrue(dir.mkdirs()||dir.isDirectory());try(FileOutputStream out=new FileOutputStream(new File(dir,"synthetic-wallet.pcw"))){out.write(pcw);}for(int i=0;i<rendered.previews().size();i++)try(FileOutputStream out=new FileOutputStream(new File(dir,String.format(Locale.ROOT,"page-%02d-kind-%d.png",i+1,rendered.pages().get(i).kind())))){assertTrue(rendered.previews().get(i).compress(Bitmap.CompressFormat.PNG,100,out));}
    }
}
