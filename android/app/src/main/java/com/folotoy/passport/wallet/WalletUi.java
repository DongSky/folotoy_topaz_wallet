package com.folotoy.passport.wallet;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Shared visual vocabulary for the three destinations and import flow. */
public final class WalletUi {
    public static final int BACKGROUND=0xff081c2b, SURFACE=0xff08273d, INK=0xffeef7f6,
        GREEN=0xff68e8b0, MUTED=0xffa0b9c8, LINE=0xff234557;
    private WalletUi() {}
    public static int dp(Context c,int n) { return Math.round(n*c.getResources().getDisplayMetrics().density); }
    public static GradientDrawable shape(Context c,int color,int radius,int border) {
        GradientDrawable d=new GradientDrawable(); d.setColor(color); d.setCornerRadius(dp(c,radius));
        if(border!=0)d.setStroke(dp(c,1),border); return d;
    }
    public static TextView text(Context c,String s,int size,boolean bold) {
        TextView v=new TextView(c); v.setText(s); v.setTextSize(size); v.setTextColor(INK);
        v.setFontFeatureSettings("tnum"); v.setLineSpacing(dp(c,3),1);
        if(bold)v.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));
        return v;
    }
    public static Button button(Context c,String label,boolean primary,View.OnClickListener action) {
        Button b=new Button(c); b.setText(label); b.setTextSize(14); b.setAllCaps(false);
        b.setTextColor(primary?BACKGROUND:INK); b.setMinHeight(dp(c,52)); b.setMinimumHeight(dp(c,52));
        b.setPadding(dp(c,12),dp(c,8),dp(c,12),dp(c,8));
        b.setBackgroundTintList(null); b.setBackground(shape(c,primary?GREEN:SURFACE,12,primary?0:LINE));
        b.setOnClickListener(action); return b;
    }
    public static LinearLayout column(Context c) { LinearLayout l=new LinearLayout(c);l.setOrientation(LinearLayout.VERTICAL);return l; }
    public static LinearLayout row(Context c) { LinearLayout l=new LinearLayout(c);l.setOrientation(LinearLayout.HORIZONTAL);l.setGravity(Gravity.CENTER_VERTICAL);return l; }
}
