package com.folotoy.passport.wallet;

import static org.junit.Assert.*;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.SharedPreferences;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import com.folotoy.passport.wallet.core.Network;
import com.folotoy.passport.wallet.core.PaymentCode;
import com.folotoy.passport.wallet.core.Profile;
import com.folotoy.passport.wallet.core.SocialLink;
import com.folotoy.passport.wallet.core.WalletBackup;
import com.folotoy.passport.wallet.core.WalletEntry;
import com.folotoy.passport.wallet.data.WalletRepository;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import org.junit.Test;
import org.junit.runner.RunWith;

/** Exercises real Android SharedPreferences; never opens the application's user wallet file. */
@RunWith(AndroidJUnit4.class)
public class RepositoryConcurrencyTest {
    private static final String TEST_PREFIX="repository_concurrency_fixture_";

    private Context isolatedContext() {
        return new ContextWrapper(InstrumentationRegistry.getInstrumentation().getTargetContext()) {
            @Override public SharedPreferences getSharedPreferences(String name,int mode) {
                return super.getSharedPreferences(TEST_PREFIX+name,mode);
            }
        };
    }
    private static WalletBackup fixture() {
        return new WalletBackup(1,new Profile("Concurrency fixture","Synthetic test profile",""),
                List.of(new SocialLink("Web","fixture","https://example.com/fixture")),
                List.of(new PaymentCode("支付宝","https://qr.alipay.com/fkx123456789fixture")),
                List.of(entry(100)),"USD");
    }
    private static WalletEntry entry(int index) {
        return new WalletEntry(Network.ETHEREUM,String.format(Locale.ROOT,"0x%040x",index),"ETH","Synthetic "+index);
    }

    @Test public void simultaneousImportsAcrossRepositoryInstancesKeepEveryEntryAndProfile() throws Exception {
        Context context=isolatedContext();
        SharedPreferences prefs=context.getSharedPreferences("wallet_private_v1",Context.MODE_PRIVATE);
        Map<String,?> previous=new HashMap<>(prefs.getAll());
        ExecutorService executor=Executors.newFixedThreadPool(16);
        try {
            WalletBackup original=fixture();new WalletRepository(context).save(original);
            CountDownLatch ready=new CountDownLatch(16),start=new CountDownLatch(1);
            List<Future<?>> imports=new ArrayList<>();
            for(int i=1;i<=16;i++) {
                final WalletEntry addition=entry(i);
                imports.add(executor.submit(()->{
                    // Separate Activity-like contexts and repository objects share the same file.
                    WalletRepository repository=new WalletRepository(isolatedContext());
                    ready.countDown();
                    if(!start.await(10,TimeUnit.SECONDS))throw new AssertionError("Import start timed out");
                    repository.addEntries(List.of(addition));return null;
                }));
            }
            assertTrue("All import workers must be ready",ready.await(10,TimeUnit.SECONDS));
            start.countDown();
            for(Future<?> task:imports)task.get(20,TimeUnit.SECONDS);
            WalletBackup actual=new WalletRepository(isolatedContext()).load();
            Set<WalletEntry> expected=new HashSet<>(original.entries());
            for(int i=1;i<=16;i++)expected.add(entry(i));
            assertEquals("Concurrent imports must not replace each other",expected,new HashSet<>(actual.entries()));
            assertEquals(expected.size(),actual.entries().size());
            assertEquals(original.profile(),actual.profile());assertEquals(original.preferredCurrency(),actual.preferredCurrency());
            assertEquals(original.socials(),actual.socials());assertEquals(original.payments(),actual.payments());
        } finally {
            executor.shutdownNow();
            try { assertTrue("Import workers must finish before restoring preferences",executor.awaitTermination(30,TimeUnit.SECONDS)); }
            finally { restore(prefs,previous); }
        }
    }

    @Test public void staleEditorCannotEraseImportedEntriesOrNewerProfileAndCurrency() {
        Context context=isolatedContext();
        SharedPreferences prefs=context.getSharedPreferences("wallet_private_v1",Context.MODE_PRIVATE);
        Map<String,?> previous=new HashMap<>(prefs.getAll());
        try {
            WalletRepository editor=new WalletRepository(context),importer=new WalletRepository(isolatedContext());
            WalletBackup original=fixture();editor.save(original);
            Profile updatedProfile=new Profile("Updated fixture","Keep this profile","");
            WalletBackup edited=new WalletBackup(1,updatedProfile,original.socials(),original.payments(),original.entries(),"CNY");
            assertTrue("A current editor must be able to save",editor.compareAndSave(original,edited));
            WalletBackup editorSnapshot=editor.load();
            importer.addEntries(List.of(entry(1),entry(2)));
            WalletBackup staleReplacement=new WalletBackup(1,original.profile(),List.of(),List.of(),editorSnapshot.entries(),"USD");
            assertFalse("A stale editor must be rejected",editor.compareAndSave(editorSnapshot,staleReplacement));
            WalletBackup actual=importer.load();
            assertEquals(Set.of(entry(100),entry(1),entry(2)),new HashSet<>(actual.entries()));
            assertEquals(updatedProfile,actual.profile());assertEquals("CNY",actual.preferredCurrency());
            assertEquals(original.socials(),actual.socials());assertEquals(original.payments(),actual.payments());
        } finally { restore(prefs,previous); }
    }

    private static void restore(SharedPreferences prefs,Map<String,?> previous) {
        SharedPreferences.Editor editor=prefs.edit().clear();
        for(Map.Entry<String,?> item:previous.entrySet()) {
            String key=item.getKey();Object value=item.getValue();
            if(value instanceof String)editor.putString(key,(String)value);
            else if(value instanceof Boolean)editor.putBoolean(key,(Boolean)value);
            else if(value instanceof Integer)editor.putInt(key,(Integer)value);
            else if(value instanceof Long)editor.putLong(key,(Long)value);
            else if(value instanceof Float)editor.putFloat(key,(Float)value);
            else if(value instanceof Set<?>) {
                Set<String> strings=new HashSet<>();for(Object string:(Set<?>)value)strings.add((String)string);
                editor.putStringSet(key,strings);
            } else throw new AssertionError("Unsupported preference type");
        }
        assertTrue("Synthetic preferences must be restored",editor.commit());
    }
}
