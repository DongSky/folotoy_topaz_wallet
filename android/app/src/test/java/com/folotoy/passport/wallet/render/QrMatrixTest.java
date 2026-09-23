package com.folotoy.passport.wallet.render;

import static org.junit.Assert.*;
import com.google.zxing.*;
import com.google.zxing.common.HybridBinarizer;
import org.junit.Test;

public class QrMatrixTest {
    @Test public void unicodePayloadRoundTripsWithFourModuleQuietZone() throws Exception {
        String payload="https://example.com/收款?name=鲍文";
        com.google.zxing.common.BitMatrix m=QrMatrix.encode(payload);
        for(int i=0;i<m.getWidth();i++){assertFalse(m.get(i,0));assertFalse(m.get(i,3));}
        int scale=5,w=m.getWidth()*scale;int[] px=new int[w*w];
        for(int y=0;y<w;y++)for(int x=0;x<w;x++)px[y*w+x]=m.get(x/scale,y/scale)?0xff000000:0xffffffff;
        Result decoded=new MultiFormatReader().decode(new BinaryBitmap(new HybridBinarizer(new RGBLuminanceSource(w,w,px))));
        assertEquals(payload,decoded.getText());
    }
}
