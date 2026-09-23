package com.folotoy.passport.wallet;

import android.app.Activity;
import android.content.Intent;
import android.text.InputType;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.TextView;
import com.folotoy.passport.wallet.core.*;
import com.folotoy.passport.wallet.data.WalletRepository;
import java.util.List;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import com.folotoy.passport.wallet.core.MnemonicWallet;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;

/** Public test vector only. Verifies APK wordlist resources and Android crypto runtime. */
@RunWith(AndroidJUnit4.class)
public class MnemonicRuntimeTest {
    @Test public void packagedWordlistDerivesKnownPublicAddresses() {
        char[] phrase = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about".toCharArray();
        try {
            var result = MnemonicWallet.derive(phrase,new char[0],0);
            assertEquals("bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu",result.bitcoin());
            assertEquals("0x9858effd232b4033e47d90003d41ec34ecaeda94",result.ethereum());
            assertEquals("HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk",result.solana());
        } finally { java.util.Arrays.fill(phrase,'\0'); }
    }
    @Test public void importWindowPreventsCaptureAndSecretsHaveNoSavedState() {
        var instrumentation = InstrumentationRegistry.getInstrumentation();
        Intent intent = new Intent(instrumentation.getTargetContext(),MnemonicImportActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        Activity activity = instrumentation.startActivitySync(intent);
        try {
            instrumentation.runOnMainSync(() -> {
                assertNotEquals(0,activity.getWindow().getAttributes().flags & WindowManager.LayoutParams.FLAG_SECURE);
                assertEquals(2,checkFields(activity.getWindow().getDecorView()));
            });
        } finally { instrumentation.runOnMainSync(activity::finish); }
    }
    @Test public void importRequiresVerificationAndPreservesExistingPublicData() throws Exception {
        var instrumentation = InstrumentationRegistry.getInstrumentation();
        var context = instrumentation.getTargetContext();
        var prefs = context.getSharedPreferences("wallet_private_v1",android.content.Context.MODE_PRIVATE);
        String original = prefs.getString("backup",null);
        WalletRepository repository = new WalletRepository(context);
        WalletEntry previous = new WalletEntry(Network.ETHEREUM,"0x52908400098527886E0F7030069857D2E4169EE7","ETH","Existing");
        WalletBackup fixture = new WalletBackup(1,new Profile("Public Test","Preserve me",""),
                List.of(new SocialLink("Web","test","https://example.com")),List.of(),List.of(previous),"CNY");
        repository.save(fixture);
        Intent intent = new Intent(context,MnemonicImportActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        Activity activity = instrumentation.startActivitySync(intent);
        String publicWords = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
        try {
            instrumentation.runOnMainSync(() -> {
                EditText input = mnemonicField(activity.getWindow().getDecorView());
                assertNotNull(input); input.setText(publicWords);
                find(activity.getWindow().getDecorView(),"下一步 · 核对地址",Button.class).performClick();
                assertEquals(0,input.length());
            });
            waitFor(() -> {
                final boolean[] ready = {false};
                instrumentation.runOnMainSync(() -> ready[0]=find(activity.getWindow().getDecorView(),"确认导入",Button.class)!=null);
                return ready[0];
            });
            instrumentation.runOnMainSync(() -> {
                View root = activity.getWindow().getDecorView();
                Button save = find(root,"确认导入",Button.class);
                assertFalse(save.isEnabled());
                assertEquals(0,checkFields(root));
                assertNotNull(find(root,"bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu",TextView.class));
                uncheckAll(root);
                find(root,"BTC",CheckBox.class).setChecked(true);
                find(root,"我已核对地址与原钱包一致",CheckBox.class).setChecked(true);
                assertTrue(save.isEnabled()); save.performClick();
            });
            waitFor(activity::isFinishing);
            WalletBackup actual = repository.load();
            assertEquals(fixture.profile(),actual.profile());
            assertEquals(fixture.socials(),actual.socials());
            assertEquals("CNY",actual.preferredCurrency());
            assertEquals(2,actual.entries().size());
            assertTrue(actual.entries().contains(previous));
            assertEquals("bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu",actual.entries().get(1).address());
            String serialized = prefs.getString("backup","");
            assertFalse(serialized.contains("abandon"));
            assertFalse(serialized.contains("mnemonic"));
            assertFalse(serialized.contains("privateKey"));
        } finally {
            instrumentation.runOnMainSync(activity::finish);
            var editor=prefs.edit(); if(original==null)editor.remove("backup"); else editor.putString("backup",original);
            assertTrue(editor.commit());
        }
    }
    private interface Ready { boolean get(); }
    private static void waitFor(Ready ready) throws InterruptedException {
        long deadline=android.os.SystemClock.elapsedRealtime()+15000;
        while(!ready.get()) {
            if(android.os.SystemClock.elapsedRealtime()>deadline)fail("Timed out waiting for import flow");
            Thread.sleep(50);
        }
    }
    private static <T extends TextView> T find(View view,String text,Class<T> type) {
        if(type.isInstance(view) && ((TextView)view).getText().toString().equals(text))return type.cast(view);
        if(view instanceof ViewGroup group)for(int i=0;i<group.getChildCount();i++) {
            T found=find(group.getChildAt(i),text,type); if(found!=null)return found;
        }
        return null;
    }
    private static EditText mnemonicField(View view) {
        if(view instanceof EditText field && (field.getInputType() & InputType.TYPE_TEXT_FLAG_MULTI_LINE)!=0)return field;
        if(view instanceof ViewGroup group)for(int i=0;i<group.getChildCount();i++) {
            EditText found=mnemonicField(group.getChildAt(i)); if(found!=null)return found;
        }
        return null;
    }
    private static void uncheckAll(View view) {
        if(view instanceof CheckBox box)box.setChecked(false);
        if(view instanceof ViewGroup group)for(int i=0;i<group.getChildCount();i++)uncheckAll(group.getChildAt(i));
    }
    private int checkFields(View view) {
        int secrets = 0;
        if(view instanceof EditText field && (field.getInputType() & InputType.TYPE_MASK_CLASS)==InputType.TYPE_CLASS_TEXT) {
            if ((field.getInputType() & InputType.TYPE_TEXT_FLAG_MULTI_LINE)!=0) {
                assertEquals(InputType.TYPE_TEXT_VARIATION_NORMAL,field.getInputType() & InputType.TYPE_MASK_VARIATION);
                assertNull(field.getTransformationMethod());
                field.setText("abandon about");
                assertEquals("abandon about",field.getText().toString());
                field.getText().clear();
            } else {
                assertEquals(InputType.TYPE_TEXT_VARIATION_PASSWORD,field.getInputType() & InputType.TYPE_MASK_VARIATION);
                assertTrue(field.getTransformationMethod() instanceof android.text.method.PasswordTransformationMethod);
            }
            assertFalse(field.isSaveEnabled());
            assertEquals(View.IMPORTANT_FOR_AUTOFILL_NO,field.getImportantForAutofill());
            assertEquals(0,field.length());
            assertTrue((field.getInputType() & InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS)!=0);
            secrets++;
        }
        if(view instanceof ViewGroup group)for(int i=0;i<group.getChildCount();i++)secrets+=checkFields(group.getChildAt(i));
        return secrets;
    }
}
