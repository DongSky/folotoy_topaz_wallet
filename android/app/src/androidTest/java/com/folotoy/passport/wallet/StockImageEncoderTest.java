package com.folotoy.passport.wallet;

import static org.junit.Assert.*;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.media.ExifInterface;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import com.folotoy.passport.wallet.ble.StockProtocol;
import com.folotoy.passport.wallet.importer.StockImageEncoder;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class StockImageEncoderTest {
    @Test public void actualJpegHasStockDimensionsAndWhiteTransparencyWithoutRecyclingCaller() {
        Bitmap source=Bitmap.createBitmap(480,480,Bitmap.Config.ARGB_8888);
        try {
            for(StockProtocol.ImageMode mode:StockProtocol.ImageMode.values()){
                byte[] encoded=StockImageEncoder.encode(source,mode);
                assertTrue(encoded.length<=StockImageEncoder.IMAGE_MAX_BYTES);
                assertEquals(0xff,encoded[0]&255);assertEquals(0xd8,encoded[1]&255);
                Bitmap jpeg=BitmapFactory.decodeByteArray(encoded,0,encoded.length);
                try {
                    assertNotNull(jpeg);
                    assertEquals(mode==StockProtocol.ImageMode.AVATAR?96:240,jpeg.getWidth());
                    assertEquals(mode==StockProtocol.ImageMode.AVATAR?160:320,jpeg.getHeight());
                    int center=jpeg.getPixel(jpeg.getWidth()/2,jpeg.getHeight()/2);
                    assertTrue(Color.red(center)>245&&Color.green(center)>245&&Color.blue(center)>245);
                }finally{if(jpeg!=null)jpeg.recycle();}
                assertFalse(source.isRecycled());
            }
        }finally{source.recycle();}
    }
    @Test public void wideSourceIsCroppedAroundCenterRatherThanStretched() {
        Bitmap source=Bitmap.createBitmap(400,100,Bitmap.Config.ARGB_8888);
        Canvas canvas=new Canvas(source);canvas.drawColor(Color.GREEN);Paint paint=new Paint();
        paint.setColor(Color.RED);canvas.drawRect(160,0,200,100,paint);
        paint.setColor(Color.BLUE);canvas.drawRect(200,0,240,100,paint);
        try{
            byte[] encoded=StockImageEncoder.encode(source,StockProtocol.ImageMode.AVATAR);
            Bitmap jpeg=BitmapFactory.decodeByteArray(encoded,0,encoded.length);
            try{
                int left=jpeg.getPixel(8,80),right=jpeg.getPixel(88,80);
                assertTrue(Color.red(left)>220&&Color.green(left)<30&&Color.blue(left)<30);
                assertTrue(Color.blue(right)>220&&Color.green(right)<30&&Color.red(right)<30);
            }finally{jpeg.recycle();}
        }finally{source.recycle();}
    }
    @Test public void allEightCameraExifOrientationsAreAppliedBeforeCrop() throws Exception {
        Bitmap source=Bitmap.createBitmap(120,80,Bitmap.Config.ARGB_8888);
        Canvas canvas=new Canvas(source);Paint paint=new Paint();
        int[] colors={Color.RED,Color.GREEN,Color.BLUE,Color.YELLOW};
        for(int i=0;i<4;i++){paint.setColor(colors[i]);canvas.drawRect((i%2)*60,(i/2)*40,(i%2+1)*60,(i/2+1)*40,paint);}
        int[][] corners={{0,1,2,3},{1,0,3,2},{3,2,1,0},{2,3,0,1},{0,2,1,3},{2,0,3,1},{3,1,2,0},{1,3,0,2}};
        File file=File.createTempFile("stock-exif-test-",".jpg",InstrumentationRegistry.getInstrumentation().getTargetContext().getCacheDir());
        try{
            for(int orientation=1;orientation<=8;orientation++){
                try(FileOutputStream out=new FileOutputStream(file)){assertTrue(source.compress(Bitmap.CompressFormat.JPEG,100,out));}
                ExifInterface metadata=new ExifInterface(file.getAbsolutePath());
                metadata.setAttribute(ExifInterface.TAG_ORIENTATION,Integer.toString(orientation));metadata.saveAttributes();
                Bitmap upright=StockImageEncoder.decode(()->new FileInputStream(file));
                try{
                    assertEquals(orientation>=5?80:120,upright.getWidth());assertEquals(orientation>=5?120:80,upright.getHeight());
                    for(int corner=0;corner<4;corner++){
                        int x=corner%2==0?12:upright.getWidth()-13,y=corner/2==0?12:upright.getHeight()-13;
                        int actual=upright.getPixel(x,y),expected=colors[corners[orientation-1][corner]];
                        assertTrue("Orientation "+orientation+", corner "+corner,Math.abs(Color.red(actual)-Color.red(expected))<25&&Math.abs(Color.green(actual)-Color.green(expected))<25&&Math.abs(Color.blue(actual)-Color.blue(expected))<25);
                    }
                    byte[] encoded=StockImageEncoder.encode(upright,StockProtocol.ImageMode.FULLSCREEN);
                    Bitmap target=BitmapFactory.decodeByteArray(encoded,0,encoded.length);
                    try{assertEquals(240,target.getWidth());assertEquals(320,target.getHeight());}finally{target.recycle();}
                }finally{upright.recycle();}
            }
        }finally{source.recycle();assertTrue(file.delete());}
    }

}
