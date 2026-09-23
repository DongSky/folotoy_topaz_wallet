package com.folotoy.passport.wallet;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import com.folotoy.passport.wallet.data.WalletRepository;
import com.folotoy.passport.wallet.render.DevicePageRenderer;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Displays actual quantized page pixels. This is not a live device screenshot. */
public final class PreviewActivity extends Activity {
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private DevicePageRenderer.Rendered rendered;
    private LinearLayout list;
    private volatile boolean destroyed;
    private int selected;
    private TextView caption;
    private ImageView image;
    private Button previous, next;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(0xff081c2b);
        getWindow().setNavigationBarColor(0xff081c2b);
        ScrollView scroll = new ScrollView(this);
        scroll.setFitsSystemWindows(true);
        scroll.setBackgroundColor(0xff081c2b);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setPadding(dp(24), dp(32), dp(24), dp(32));
        scroll.addView(list);
        setContentView(scroll);
        Button back = button("‹ 返回", v -> finish());
        list.addView(back);
        TextView heading = text("设备页面预览", 26, true);
        list.addView(heading);
        list.addView(text("实际量化像素 · 未同步到设备\n完整设备界面仍在开发，以下展示内容区域。", 13, false));
        caption = text("正在生成…", 14, false);
        caption.setPadding(0, dp(20), 0, dp(12));
        list.addView(caption);
        image = new ImageView(this);
        image.setAdjustViewBounds(true);
        image.setScaleType(ImageView.ScaleType.FIT_CENTER);
        LinearLayout.LayoutParams ip = new LinearLayout.LayoutParams(-1, dp(360));
        ip.setMargins(0, 0, 0, dp(20));
        list.addView(image, ip);
        LinearLayout controls = new LinearLayout(this);
        previous = button("↑ 上一页", v -> select(selected - 1));
        next = button("↓ 下一页", v -> select(selected + 1));
        previous.setEnabled(false); next.setEnabled(false);
        controls.addView(previous, new LinearLayout.LayoutParams(0, dp(52), 1));
        controls.addView(next, new LinearLayout.LayoutParams(0, dp(52), 1));
        list.addView(controls);
        list.addView(text("二维码保留完整白色留白。资产页含余额和估值，请留意周围环境。", 12, false));
        worker.execute(() -> {
            try {
                WalletRepository repository = new WalletRepository(getApplicationContext());
                var cache = repository.cache();
                DevicePageRenderer.Rendered result = DevicePageRenderer.render(repository.load(), cache, cache);
                runOnUiThread(() -> {
                    if (destroyed || isFinishing()) { recycle(result); return; }
                    rendered = result;
                    select(0);
                });
            } catch (Exception error) {
                runOnUiThread(() -> {
                    if (!destroyed && !isFinishing()) new AlertDialog.Builder(this)
                        .setTitle("无法生成预览").setMessage(error.getMessage())
                        .setPositiveButton("返回", (dialog, which) -> finish()).show();
                });
            }
        });
    }

    private void select(int position) {
        if (rendered == null) return;
        selected = Math.floorMod(position, rendered.pages().size());
        var page = rendered.pages().get(selected);
        caption.setText(kindName(page.kind()) + "  ·  " + (selected + 1) + " / " + rendered.pages().size()
                + (page.privatePage() ? "  ·  财务数据" : ""));
        image.setImageBitmap(rendered.previews().get(selected));
        image.setContentDescription(kindName(page.kind()) + "预览");
        previous.setEnabled(rendered.pages().size() > 1); next.setEnabled(rendered.pages().size() > 1);
    }

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private TextView text(String value, int size, boolean bold) {
        TextView t = new TextView(this); t.setText(value); t.setTextSize(size);
        t.setTextColor(bold ? 0xffeef7f6 : 0xff93aeb9); t.setPadding(0, dp(8), 0, dp(8));
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }
    private Button button(String title, View.OnClickListener click) {
        Button b = new Button(this); b.setText(title); b.setAllCaps(false);
        b.setTextColor(0xff68e8b0); b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xff153448));
        b.setMinHeight(dp(48)); b.setOnClickListener(click); return b;
    }
    private static String kindName(int kind) {
        return switch (kind) {
            case 0 -> "名片"; case 1 -> "社交账号"; case 2 -> "收款码";
            case 3 -> "加密货币地址"; case 4 -> "资产估值"; default -> "页面";
        };
    }
    private static void recycle(DevicePageRenderer.Rendered pages) {
        for (var bitmap : pages.previews()) bitmap.recycle();
    }
    @Override protected void onDestroy() {
        destroyed = true; worker.shutdownNow(); image.setImageDrawable(null); list.removeAllViews();
        if (rendered != null) recycle(rendered);
        super.onDestroy();
    }
}
