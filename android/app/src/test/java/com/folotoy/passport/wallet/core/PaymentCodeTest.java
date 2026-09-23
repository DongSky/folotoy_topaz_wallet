package com.folotoy.passport.wallet.core;

import org.junit.Test;
import static org.junit.Assert.*;

public class PaymentCodeTest {
    @Test public void recognizesProvidersAndPreservesPayloadExactly() {
        String alipay="https://qr.alipay.com/fkx123456789AbCd";
        PaymentCode a=new PaymentCode("支付宝",alipay);
        assertEquals(PaymentCode.Provider.ALIPAY,a.provider()); assertEquals(alipay,a.payload());
        String wechat="wxp://f2f0AbCd_EfG-123456789";
        PaymentCode w=new PaymentCode("微信",wechat);
        assertEquals(PaymentCode.Provider.WECHAT,w.provider()); assertEquals(wechat,w.payload());
    }
    @Test public void rejectsGenericAuthorizationAndSpoofedCodes() {
        for (String value : new String[]{"sample-payment","123456789012345678","https://example.com/a",
                "https://qr.alipay.com.evil.test/fkx123456789","https://qr.alipay.com@evil.test/fkx123456789",
                "http://qr.alipay.com/fkx123456789","https://qr.alipay.com:443/fkx123456789",
                "https://qr.alipay.com/fkx123456789?redirect=evil","https://qr.alipay.com/fkx123456789#x",
                "https://qr.alipay.com/a","https://qr.alipay.com/order123456789","https://qr.alipay.com/%66kx123456789",
                "wxp://pay1234567890","weixin://wxpay/bizpayurl?pr=123456789",
                "wxp://f2f0abcdefgh/other","wxp://f2f0abcdefgh?amount=1",
                " https://qr.alipay.com/fkx123456789","https://qr.alipay.com/fkx123456789\n"}) {
            assertThrows(value,IllegalArgumentException.class,()->new PaymentCode("Code",value));
        }
    }
    @Test public void backupCannotBypassProviderValidation() {
        WalletBackup valid=new WalletBackup(1,new Profile("","",""),java.util.List.of(),
            java.util.List.of(new PaymentCode("支付宝","https://qr.alipay.com/fkx123456789")),java.util.List.of());
        String encoded=BackupCodec.encode(valid);
        assertEquals(valid,BackupCodec.decode(encoded));
        assertThrows(IllegalArgumentException.class,()->BackupCodec.decode(encoded.replace("https://qr.alipay.com/fkx123456789","arbitrary-QR")));
    }
}
