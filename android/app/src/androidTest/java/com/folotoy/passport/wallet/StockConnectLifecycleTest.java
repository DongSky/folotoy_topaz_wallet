package com.folotoy.passport.wallet;

import static org.junit.Assert.*;
import android.os.Handler;
import android.os.Looper;
import android.widget.TextView;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import com.folotoy.passport.wallet.ble.StockProtocol;
import com.folotoy.passport.wallet.ble.StockTransferSession;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class StockConnectLifecycleTest {
    @Test public void synchronousWriteRejectionDoesNotScheduleNullSessionTick() throws Exception {
        AtomicReference<Throwable> failure=new AtomicReference<>();
        CountDownLatch observed=new CountDownLatch(1);
        InstrumentationRegistry.getInstrumentation().runOnMainSync(()->{
            try{
                // Unattached Activity: no radio, no permissions, no actual connection or device write.
                StockConnectActivity activity=new StockConnectActivity();
                set(activity,"state",new TextView(InstrumentationRegistry.getInstrumentation().getTargetContext()));
                set(activity,"ready",true);
                Method start=StockConnectActivity.class.getDeclaredMethod("startTransfer",List.class,String.class);start.setAccessible(true);
                // GATT absent simulates the immediate rejected-write path, which clears transfer in pump().
                start.invoke(activity,StockProtocol.profile("Fixture","Synthetic"),"test");
                Field active=StockConnectActivity.class.getDeclaredField("transfer");active.setAccessible(true);assertNull(active.get(activity));
                Method tick=StockConnectActivity.class.getDeclaredMethod("tick",StockTransferSession.class);tick.setAccessible(true);tick.invoke(activity,new Object[]{null});
                new Handler(Looper.getMainLooper()).postDelayed(()->observed.countDown(),350);
            }catch(Throwable e){failure.set(e);observed.countDown();}
        });
        assertTrue("Main looper must survive beyond the 200ms transfer tick",observed.await(5,TimeUnit.SECONDS));
        if(failure.get()!=null)throw new AssertionError(failure.get());
    }
    private static void set(Object target,String field,Object value)throws Exception{
        Field f=target.getClass().getDeclaredField(field);f.setAccessible(true);f.set(target,value);
    }
}
