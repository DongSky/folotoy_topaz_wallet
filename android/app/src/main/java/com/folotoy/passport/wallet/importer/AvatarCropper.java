package com.folotoy.passport.wallet.importer;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.util.Base64;

public final class AvatarCropper {
    private AvatarCropper(){}
    public static Bitmap centerSquare(Bitmap source,int size){int s=Math.min(source.getWidth(),source.getHeight()),x=(source.getWidth()-s)/2,y=(source.getHeight()-s)/2;Bitmap out=Bitmap.createBitmap(size,size,Bitmap.Config.ARGB_8888);new Canvas(out).drawBitmap(source,new Rect(x,y,x+s,y+s),new Rect(0,0,size,size),null);return out;}
    public static void validateEncoded(String value){if(value==null||value.isEmpty())return;byte[] bytes=Base64.decode(value,Base64.DEFAULT);if(bytes.length>512*1024)throw new IllegalArgumentException("Avatar exceeds 512 KB");android.graphics.BitmapFactory.Options o=new android.graphics.BitmapFactory.Options();o.inJustDecodeBounds=true;android.graphics.BitmapFactory.decodeByteArray(bytes,0,bytes.length,o);if(o.outWidth<1||o.outHeight<1||o.outWidth>4096||o.outHeight>4096)throw new IllegalArgumentException("Avatar image is invalid or exceeds 4096×4096");}
}
