package com.folotoy.passport.wallet.render;

public final class PageRules {
    public record QrLayout(int scale,int pixels,int quietModules){}
    private PageRules(){}
    public static QrLayout qrLayout(int dataModules,int availablePixels){
        int all=dataModules+8, scale=availablePixels/all;
        if(scale<2) throw new IllegalArgumentException("QR is too dense; use a shorter receiving payload");
        return new QrLayout(scale,all*scale,4);
    }
}
