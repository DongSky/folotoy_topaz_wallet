package com.folotoy.passport.wallet.core;

import java.net.URI;

/** Only recognized external personal receiving QR formats; never payment authorization codes. */
public record PaymentCode(String label, String payload) {
    public enum Provider {
        ALIPAY("支付宝"), WECHAT("微信支付");
        private final String label;
        Provider(String label) { this.label = label; }
        public String label() { return label; }
    }

    public PaymentCode {
        if (label == null || label.isBlank() || label.length() > 80)
            throw new IllegalArgumentException("收款码名称无效");
        detectProvider(payload);
    }

    public Provider provider() { return detectProvider(payload); }

    /** Validation does not normalize or rewrite any part of the imported payload. */
    public static Provider detectProvider(String payload) {
        if (payload == null || payload.length() > 4096 || payload.isBlank()) throw invalid();
        for (int i = 0; i < payload.length(); i++)
            if (Character.isWhitespace(payload.charAt(i)) || Character.isISOControl(payload.charAt(i))) throw invalid();
        final URI uri;
        try { uri = new URI(payload); } catch (Exception e) { throw invalid(); }
        if (uri.getRawUserInfo() != null || uri.getPort() != -1 || uri.getRawFragment() != null
                || uri.getRawQuery() != null) throw invalid();
        String scheme = uri.getScheme(), host = uri.getHost();
        String path = uri.getRawPath();
        if ("https".equalsIgnoreCase(scheme) && "qr.alipay.com".equalsIgnoreCase(host)
                && path != null && path.matches("/fkx[A-Za-z0-9_-]{8,253}")) return Provider.ALIPAY;
        // WeChat's receiving token is the URI authority, not a web domain.
        if ("wxp".equalsIgnoreCase(scheme) && uri.getRawAuthority() != null
                && uri.getRawAuthority().matches("f2f[A-Za-z0-9_-]{8,256}")
                && (path == null || path.isEmpty())) return Provider.WECHAT;
        throw invalid();
    }

    private static IllegalArgumentException invalid() {
        return new IllegalArgumentException("请选择支付宝或微信的个人收款码图片；不支持付款码、订单码或其他二维码");
    }
}
