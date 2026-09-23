package com.folotoy.passport.wallet.render;

import com.google.zxing.EncodeHintType;
import com.google.zxing.common.BitMatrix;
import com.google.zxing.qrcode.decoder.ErrorCorrectionLevel;
import com.google.zxing.qrcode.encoder.ByteMatrix;
import com.google.zxing.qrcode.encoder.Encoder;
import java.util.Map;

public final class QrMatrix {
    private QrMatrix(){}
    public static BitMatrix encode(String payload)throws Exception{
        ByteMatrix raw=Encoder.encode(payload,ErrorCorrectionLevel.M,Map.of(EncodeHintType.CHARACTER_SET,"UTF-8")).getMatrix();
        BitMatrix out=new BitMatrix(raw.getWidth()+8,raw.getHeight()+8);
        for(int y=0;y<raw.getHeight();y++)for(int x=0;x<raw.getWidth();x++)if(raw.get(x,y)==1)out.set(x+4,y+4);
        return out;
    }
}
