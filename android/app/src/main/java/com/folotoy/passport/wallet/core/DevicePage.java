package com.folotoy.passport.wallet.core;

import java.util.Arrays;

public final class DevicePage {
    public static final int KIND_CARD=0,KIND_SOCIAL=1,KIND_PAYMENT=2,KIND_CRYPTO=3,KIND_ASSET=4;
    private final int kind; private final boolean privatePage; private final int[] palette; private final byte[] pixels;
    public DevicePage(int kind, boolean privatePage, int[] palette, byte[] pixels) {
        if(kind<0||kind>4) throw new IllegalArgumentException("Unknown page kind");
        if(kind==KIND_ASSET&&!privatePage) throw new IllegalArgumentException("Asset pages must be private");
        if(kind!=KIND_ASSET&&privatePage) throw new IllegalArgumentException("Only asset pages may be private");
        if(palette==null||palette.length!=16) throw new IllegalArgumentException("Palette must have 16 colors");
        for(int c:palette) if((c>>>24)!=0xff) throw new IllegalArgumentException("Palette alpha must be 255");
        if(pixels==null||pixels.length!=Pcw1.PAGE_PIXEL_BYTES) throw new IllegalArgumentException("Wrong pixel bytes");
        this.kind=kind; this.privatePage=privatePage; this.palette=palette.clone(); this.pixels=pixels.clone();
    }
    public int kind(){return kind;} public boolean privatePage(){return privatePage;}
    public int[] palette(){return palette.clone();} public byte[] pixels(){return pixels.clone();}
}
