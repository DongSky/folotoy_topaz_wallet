package com.folotoy.passport.wallet.importer;

import android.graphics.Bitmap;
import com.folotoy.passport.wallet.core.PaymentCode;
import com.google.zxing.BarcodeFormat;
import com.google.zxing.BinaryBitmap;
import com.google.zxing.DecodeHintType;
import com.google.zxing.MultiFormatReader;
import com.google.zxing.RGBLuminanceSource;
import com.google.zxing.common.HybridBinarizer;
import java.util.List;
import java.util.Map;

public final class QrImageDecoder {
    private QrImageDecoder() {}
    public static String decode(Bitmap b) throws Exception {
        int w=b.getWidth(), h=b.getHeight(); int[] px=new int[w*h];
        b.getPixels(px,0,w,0,0,w,h);
        String payload = new MultiFormatReader().decode(new BinaryBitmap(new HybridBinarizer(
            new RGBLuminanceSource(w,h,px))), Map.of(DecodeHintType.POSSIBLE_FORMATS,
            List.of(BarcodeFormat.QR_CODE), DecodeHintType.TRY_HARDER, true)).getText();
        return payload;
    }
    public static void requireReusableReceivePayload(String value) { PaymentCode.detectProvider(value); }
}
