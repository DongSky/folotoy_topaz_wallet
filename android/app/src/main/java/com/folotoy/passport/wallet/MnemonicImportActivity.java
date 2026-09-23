package com.folotoy.passport.wallet;

import android.app.Activity;
import android.content.res.ColorStateList;
import android.os.Bundle;
import android.text.InputFilter;
import android.text.InputType;
import android.text.method.PasswordTransformationMethod;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import com.folotoy.passport.wallet.core.*;
import com.folotoy.passport.wallet.data.WalletRepository;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import static com.folotoy.passport.wallet.WalletUi.*;

/** Ephemeral, offline mnemonic import. Only public addresses are persisted. */
public final class MnemonicImportActivity extends Activity {
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private LinearLayout content;
    private EditText phrase, passphrase, account;
    private TextView status;
    private Button next, save;
    private CheckBox verified;
    private MnemonicWallet.Accounts accounts;
    private boolean busy;
    private final Map<AssetRegistry.Asset, CheckBox> selections = new LinkedHashMap<>();

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
        getWindow().setStatusBarColor(BACKGROUND);
        getWindow().setNavigationBarColor(BACKGROUND);
        showInput();
    }
    private void page(String step, String title) {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(BACKGROUND);
        scroll.setSaveEnabled(false);
        scroll.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS);
        content = column(this);
        content.setPadding(dp(this,24),dp(this,20),dp(this,24),dp(this,32));
        content.setSaveEnabled(false);
        scroll.addView(content);
        setContentView(scroll);
        // targetSdk 35 enforces edge-to-edge; keep controls clear of both system bars and IME.
        if (android.os.Build.VERSION.SDK_INT >= 30) scroll.setOnApplyWindowInsetsListener((v,insets)-> {
            android.graphics.Insets bars = insets.getInsets(android.view.WindowInsets.Type.systemBars()
                    | android.view.WindowInsets.Type.ime());
            v.setPadding(bars.left,bars.top,bars.right,bars.bottom);
            return insets;
        });
        if (android.os.Build.VERSION.SDK_INT < 30) scroll.setOnApplyWindowInsetsListener((v,insets)-> {
            v.setPadding(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom());
            return insets;
        });
        TextView eyebrow = text(this,step,12,true); eyebrow.setTextColor(GREEN); add(eyebrow,12);
        add(text(this,title,28,true),20);
    }
    private void showInput() {
        accounts = null;
        selections.clear();
        page("导入钱包 · 1 / 2", "导入助记词");
        add(text(this,"在本机生成收款地址。助记词和密码不会保存、上传或发送到卡片。导入后仍需原钱包才能转账。",14,false),20);
        add(text(this,"英文助记词",14,true),8);
        phrase = ephemeralInput("按顺序输入单词，用空格分隔（12 / 15 / 18 / 21 / 24 词）",true);
        add(phrase,16);
        add(text(this,"BIP39 密码（可选）",14,true),8);
        passphrase = ephemeralInput("原钱包未设置则留空",false); add(passphrase,16);
        add(text(this,"账户序号",14,true),8);
        account = new EditText(this);
        styleField(account); account.setInputType(InputType.TYPE_CLASS_NUMBER);
        account.setText("0"); account.setSingleLine(true); add(account,8);
        TextView hint = text(this,"从 0 开始。Bitcoin 使用 BIP84 首个收款地址；EVM 共用一个地址；Solana 使用四级 hardened 路径。不同钱包的路径可能不同，请在下一步核对。",12,false);
        hint.setTextColor(MUTED); add(hint,20);
        status = text(this,"",14,false); add(status,12);
        next = button(this,"下一步 · 核对地址",true,v -> derive()); add(next,12);
        add(button(this,"取消",false,v -> finish()),0);
    }
    private EditText ephemeralInput(String hint, boolean multiline) {
        EditText field = new EditText(this); styleField(field);
        field.setHint(hint);
        field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                | (multiline ? InputType.TYPE_TEXT_FLAG_MULTI_LINE : InputType.TYPE_TEXT_VARIATION_PASSWORD));
        field.setImeOptions(EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        field.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO);
        field.setSaveEnabled(false); field.setFreezesText(false);
        field.setFilters(new InputFilter[]{new InputFilter.LengthFilter(2048)});
        if (multiline) { field.setMinLines(3); field.setGravity(android.view.Gravity.TOP); }
        else field.setSingleLine(true);
        // setSingleLine installs its own transformation; select visibility last.
        field.setTransformationMethod(multiline ? null : PasswordTransformationMethod.getInstance());
        return field;
    }
    private void styleField(EditText field) {
        field.setTextColor(INK); field.setHintTextColor(MUTED); field.setTextSize(15);
        field.setPadding(dp(this,14),dp(this,14),dp(this,14),dp(this,14));
        field.setBackgroundTintList(null); field.setBackground(shape(this,SURFACE,12,LINE));
        field.setSaveEnabled(false);
    }
    private void derive() {
        if (busy) return;
        final int index;
        try { index = Integer.parseInt(account.getText().toString()); if(index<0)throw new NumberFormatException(); }
        catch(NumberFormatException e) { status.setText("请输入 0 到 2147483647 之间的账户序号"); return; }
        final char[] words = copy(phrase), password = copy(passphrase);
        clearFields();
        ((InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(content.getWindowToken(),0);
        busy = true; next.setEnabled(false); phrase.setEnabled(false); passphrase.setEnabled(false); account.setEnabled(false);
        status.setText("正在本机生成地址…");
        worker.execute(() -> {
            MnemonicWallet.Accounts result = null;
            String error = null;
            try { result = MnemonicWallet.derive(words,password,index); }
            catch(IllegalArgumentException e) { error = e.getMessage(); }
            catch(RuntimeException e) { error = "地址生成失败，请重试"; }
            finally { Arrays.fill(words,'\0'); Arrays.fill(password,'\0'); }
            final MnemonicWallet.Accounts derived = result;
            final String message = error;
            runOnUiThread(() -> {
                if(isFinishing() || isDestroyed()) return;
                busy = false;
                if(derived != null) { accounts = derived; showReview(); }
                else { next.setEnabled(true); phrase.setEnabled(true); passphrase.setEnabled(true); account.setEnabled(true); status.setText(message); }
            });
        });
    }
    private void showReview() {
        phrase = null; passphrase = null; account = null;
        page("导入钱包 · 2 / 2", "核对地址与资产");
        add(text(this,"请与原钱包核对地址。BIP39 密码没有“错误”提示，任何密码都会产生不同的钱包。",14,false),20);
        addressBlock("Bitcoin · Native SegWit",accounts.bitcoin(),accounts.btcPath());
        addressBlock("Ethereum / EVM",accounts.ethereum(),accounts.evmPath());
        addressBlock("Solana",accounts.solana(),accounts.solPath());
        add(text(this,"选择要添加的网络和资产",18,true),12);
        selections.clear();
        for(Network network : Network.values()) {
            add(text(this,network.label(),15,true),4);
            for(AssetRegistry.Asset asset : AssetRegistry.forNetwork(network)) {
                String provenance = "native".equals(asset.origin()) ? "" : " · " + asset.origin();
                CheckBox choice = checkbox(asset.symbol()+provenance);
                choice.setChecked(asset.contractOrMint()==null);
                selections.put(asset,choice); add(choice,4);
            }
        }
        status = text(this,"仅保存所选资产的公开收款地址。",13,false); add(status,12);
        verified = checkbox("我已核对地址与原钱包一致"); add(verified,16);
        save = button(this,"确认导入",true,v -> save()); save.setEnabled(false); add(save,12);
        verified.setOnCheckedChangeListener((button,checked) -> save.setEnabled(checked && !busy));
        add(button(this,"返回重新输入",false,v -> { if(!busy)showInput(); }),12);
        add(button(this,"取消",false,v -> { if(!busy)finish(); }),0);
    }
    private CheckBox checkbox(String label) {
        CheckBox box = new CheckBox(this); box.setText(label); box.setTextSize(14); box.setTextColor(INK);
        box.setButtonTintList(ColorStateList.valueOf(GREEN)); box.setMinHeight(dp(this,48));
        return box;
    }
    private void addressBlock(String title,String address,String path) {
        LinearLayout card = column(this);
        card.setPadding(dp(this,16),dp(this,16),dp(this,16),dp(this,16));
        card.setBackground(shape(this,SURFACE,14,LINE));
        card.addView(text(this,title,15,true));
        TextView value = text(this,address,14,false); value.setPadding(0,dp(this,10),0,dp(this,8));
        value.setTextIsSelectable(true); card.addView(value);
        TextView derivation = text(this,path,12,false); derivation.setTextColor(MUTED); card.addView(derivation);
        add(card,12);
    }
    private void save() {
        if(busy || accounts==null || verified==null || !verified.isChecked()) return;
        ArrayList<WalletEntry> additions = new ArrayList<>();
        for(var row : selections.entrySet()) if(row.getValue().isChecked()) {
            AssetRegistry.Asset a = row.getKey();
            additions.add(new WalletEntry(a.network(),accounts.address(a.network()),a.symbol(),"导入的钱包"));
        }
        if(additions.isEmpty()) { status.setText("请至少选择一种资产"); return; }
        busy = true; save.setEnabled(false); status.setText("正在保存公开地址…");
        worker.execute(() -> {
            boolean saved = false;
            try {
                WalletRepository repository = new WalletRepository(getApplicationContext());
                repository.addEntries(additions);
                saved = true;
            } catch(RuntimeException ignored) { /* Never discard existing data on a save failure. */ }
            final boolean success = saved;
            runOnUiThread(() -> {
                if(isFinishing() || isDestroyed()) return;
                busy = false;
                if(success) { setResult(RESULT_OK); finish(); }
                else { save.setEnabled(true); status.setText("保存失败。请检查现有数据或是否已达到 64 个资产上限。"); }
            });
        });
    }
    private static char[] copy(EditText field) {
        char[] out = new char[field.length()]; field.getText().getChars(0,field.length(),out,0); return out;
    }
    private void clearFields() {
        if(phrase!=null)phrase.getText().clear(); if(passphrase!=null)passphrase.getText().clear();
    }
    private void add(View view,int gap) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1,-2); p.bottomMargin=dp(this,gap); content.addView(view,p);
    }
    @Override protected void onStop() { clearFields(); super.onStop(); }
    @Override protected void onDestroy() {
        clearFields(); accounts=null;
        // Let submitted work finish its finally block so its private char buffers are cleared.
        worker.shutdown(); super.onDestroy();
    }
    @Override public void onBackPressed() { if(busy)return; if(accounts!=null)showInput(); else super.onBackPressed(); }
}
