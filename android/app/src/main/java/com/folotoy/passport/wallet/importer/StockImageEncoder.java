package com.folotoy.passport.wallet.importer;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Matrix;
import android.media.ExifInterface;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import com.folotoy.passport.wallet.ble.StockProtocol;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.IOException;

/** Worker-only stock JPEG preparation. Caller retains ownership of source bitmap. */
public final class StockImageEncoder {
    public static final int IMAGE_MAX_BYTES=128*1024-4096;
    private StockImageEncoder(){}
    public interface Input { InputStream open() throws IOException; }
    /** Opens separate bounded decode/metadata streams. Returned bitmap is owned by caller. */
    public static Bitmap decode(Input input) throws IOException {
        BitmapFactory.Options bounds=new BitmapFactory.Options();bounds.inJustDecodeBounds=true;
        try(InputStream in=input.open()){
            if(in==null)throw new IOException("无法打开图片。");BitmapFactory.decodeStream(in,null,bounds);
        }
        if(bounds.outWidth<1||bounds.outHeight<1||bounds.outWidth>32768||bounds.outHeight>32768)
            throw new IOException("图片无效或尺寸过大。");
        int orientation=ExifInterface.ORIENTATION_NORMAL;
        try(InputStream in=input.open()){
            if(in!=null)orientation=new ExifInterface(in).getAttributeInt(ExifInterface.TAG_ORIENTATION,ExifInterface.ORIENTATION_NORMAL);
        }catch(IOException|IllegalArgumentException ignored){ /* Images without supported EXIF remain upright. */ }
        BitmapFactory.Options options=new BitmapFactory.Options();options.inSampleSize=1;
        while(bounds.outWidth/options.inSampleSize>2048||bounds.outHeight/options.inSampleSize>2048)options.inSampleSize*=2;
        Bitmap bitmap;
        try(InputStream in=input.open()){
            if(in==null)throw new IOException("无法打开图片。");bitmap=BitmapFactory.decodeStream(in,null,options);
        }
        if(bitmap==null)throw new IOException("图片解码失败。");
        Matrix matrix=new Matrix();
        switch(orientation){
            case ExifInterface.ORIENTATION_FLIP_HORIZONTAL:matrix.setScale(-1,1);break;
            case ExifInterface.ORIENTATION_ROTATE_180:matrix.setRotate(180);break;
            case ExifInterface.ORIENTATION_FLIP_VERTICAL:matrix.setScale(1,-1);break;
            case ExifInterface.ORIENTATION_TRANSPOSE:matrix.setRotate(90);matrix.postScale(-1,1);break;
            case ExifInterface.ORIENTATION_ROTATE_90:matrix.setRotate(90);break;
            case ExifInterface.ORIENTATION_TRANSVERSE:matrix.setRotate(90);matrix.postScale(1,-1);break;
            case ExifInterface.ORIENTATION_ROTATE_270:matrix.setRotate(270);break;
            default:return bitmap;
        }
        try{
            Bitmap upright=Bitmap.createBitmap(bitmap,0,0,bitmap.getWidth(),bitmap.getHeight(),matrix,true);
            if(upright!=bitmap)bitmap.recycle();return upright;
        }catch(RuntimeException|Error error){bitmap.recycle();throw error;}
    }
    public static byte[] encode(Bitmap source,StockProtocol.ImageMode mode) {
        if(source==null||source.isRecycled()||mode==null)throw new IllegalArgumentException("图片不可用");
        int width=mode==StockProtocol.ImageMode.AVATAR?96:240;
        int height=mode==StockProtocol.ImageMode.AVATAR?160:320;
        Bitmap output=Bitmap.createBitmap(width,height,Bitmap.Config.ARGB_8888);
        try {
            int sw=source.getWidth(),sh=source.getHeight(),cropWidth=sw,cropHeight=sh;
            if((long)sw*height>(long)sh*width)cropWidth=Math.max(1,(int)((long)sh*width/height));
            else cropHeight=Math.max(1,(int)((long)sw*height/width));
            int left=(sw-cropWidth)/2,top=(sh-cropHeight)/2;
            Canvas canvas=new Canvas(output);canvas.drawColor(Color.WHITE);
            canvas.drawBitmap(source,new Rect(left,top,left+cropWidth,top+cropHeight),new Rect(0,0,width,height),new Paint(Paint.FILTER_BITMAP_FLAG));
            for(int quality=90;quality>=30;quality-=15){
                ByteArrayOutputStream bytes=new ByteArrayOutputStream();
                if(!output.compress(Bitmap.CompressFormat.JPEG,quality,bytes))throw new IllegalArgumentException("JPEG 编码失败");
                if(bytes.size()<=IMAGE_MAX_BYTES)return bytes.toByteArray();
            }
            throw new IllegalArgumentException("图片压缩后仍超过设备容量，请选择简单一些的图片");
        }finally{output.recycle();}
    }
}
