package com.folotoy.passport.wallet.render;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.util.Base64;
import com.folotoy.passport.wallet.core.*;
import com.google.zxing.common.BitMatrix;
import java.math.BigDecimal;
import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Rasterizes validated content into exactly the same palette shown in preview. */
public final class DevicePageRenderer {
    public static final int[] PALETTE = {
        0xff08273d, 0xffeef7f6, 0xff68e8b0, 0xffeebd68,
        0xffe63946, 0xff2a9d8f, 0xff6c5ce7, 0xffffffff,
        0xff93aeb9, 0xffb3cbd3, 0xff74c0fc, 0xffff8787,
        0xff8ce99a, 0xffffe066, 0xffb197fc, 0xff000000
    };
    private static final DateTimeFormatter TIME =
        DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm 'UTC'").withZone(ZoneOffset.UTC);
    private static final long STALE_SECONDS = 3600;
    private static final int BODY_LINES = 11;
    public record Rendered(List<DevicePage> pages, List<Bitmap> previews) {}

    private DevicePageRenderer() {}

    public static Rendered render(WalletBackup wallet, Map<String, CachedAmount> balances,
                                  Map<String, CachedAmount> prices) {
        validateGlyphs(wallet);
        List<DevicePage> pages = new ArrayList<>();
        List<Bitmap> previews = new ArrayList<>();
        add(pages, previews, card(wallet.profile(), wallet.socials()), DevicePage.KIND_CARD, false);
        addText(pages, previews, "个人介绍", wallet.profile().nickname() + "\n" +
                wallet.profile().bio(), DevicePage.KIND_SOCIAL, false);
        for (SocialLink social : wallet.socials()) {
            addText(pages, previews, "社交账号", social.service() + "\n" + social.handle() +
                    (social.url().isBlank() ? "" : "\n" + social.url()), DevicePage.KIND_SOCIAL, false);
            if (!social.url().isBlank()) {
                add(pages, previews, qrPage(social.service(), social.url(), "社交主页"),
                    DevicePage.KIND_SOCIAL, false);
            }
        }
        for (PaymentCode payment : wallet.payments()) {
            add(pages, previews, qrPage(payment.label(), payment.payload(), "请核对收款人后付款"),
                DevicePage.KIND_PAYMENT, false);
        }
        List<WalletEntry> unique = WalletEntries.deduplicate(wallet.entries());
        // Receiving address is shared by native coin and registered tokens on a
        // network. Group the QR but list every accepted asset explicitly.
        Map<String, List<WalletEntry>> receiving = new LinkedHashMap<>();
        for (WalletEntry entry : unique) {
            String address = entry.network().evm() ? entry.address().toLowerCase(java.util.Locale.ROOT)
                                                  : entry.address();
            receiving.computeIfAbsent(entry.network().id() + ":" + address,
                                      ignored -> new ArrayList<>()).add(entry);
        }
        for (List<WalletEntry> group : receiving.values()) {
            WalletEntry first = group.get(0);
            StringBuilder details = new StringBuilder(first.network().label())
                .append("\n").append(first.label()).append("\n").append(first.address());
            for (WalletEntry entry : group) {
                AssetRegistry.Asset asset = AssetRegistry.get(entry.network(), entry.asset());
                details.append("\n").append(asset.symbol()).append(" · ").append(asset.origin());
            }
            addText(pages, previews, "收款地址", details.toString(), DevicePage.KIND_CRYPTO, false);
            add(pages, previews, qrPage(first.network().label(), first.address(), "请核对网络与完整地址"),
                DevicePage.KIND_CRYPTO, false);
        }
        Instant now = Instant.now();
        String currency = wallet.preferredCurrency();
        if (!unique.isEmpty()) {
            Valuation.Result total = Valuation.total(unique, balances, prices, currency, now, STALE_SECONDS);
            Bitmap bitmap = base(); Canvas canvas = new Canvas(bitmap);
            canvas.drawText("资产总览", 12, 25, paint(PALETTE[2], 14, true));
            canvas.drawText(currency + " · 估值", 12, 63, paint(PALETTE[8], 12, false));
            drawAmount(canvas, total.includedEntries() == 0 ? "—" : money(total.total()), 99);
            canvas.drawText(total.incomplete() ? "部分数据缺失或已过期" : "已导入资产的合计估值", 12, 124, paint(PALETTE[8], 11, false));
            canvas.drawRect(12, 145, 204, 146, paint(0xff234557, 1, false));
            canvas.drawText(unique.size() + " 项资产", 12, 170, paint(PALETTE[1], 14, true));
            canvas.drawText("余额和价格时间见资产详情", 12, 198, paint(PALETTE[9], 11, false));
            canvas.drawText("仅供参考 · 不代表可用余额", 12, 239, paint(PALETTE[8], 10, false));
            add(pages, previews, bitmap, DevicePage.KIND_ASSET, true);
        }
        for (WalletEntry entry : unique) {
            CachedAmount balance = balances.get(entry.key());
            CachedAmount price = prices.get(entry.priceKey(currency));
            boolean available = present(balance) && present(price);
            boolean stale = !available || balance.stale(now, STALE_SECONDS) || price.stale(now, STALE_SECONDS)
                || balance.error() != null || price.error() != null;
            Bitmap bitmap = base(); Canvas canvas = new Canvas(bitmap);
            canvas.drawText(entry.asset(), 12, 28, paint(PALETTE[2], 18, true));
            Paint networkFont = paint(PALETTE[8], 11, false);
            String network = fit(entry.network().label(), networkFont, 118);
            canvas.drawText(network, 204 - networkFont.measureText(network), 27, networkFont);
            canvas.drawText(currency + " · 估值", 12, 63, paint(PALETTE[8], 11, false));
            drawAmount(canvas, available ? money(balance.value().multiply(price.value())) : "—", 99);
            canvas.drawText(stale ? "数据不完整 / 已过期" : "已更新", 12, 120, paint(PALETTE[8], 10, false));
            canvas.drawRect(12, 137, 204, 138, paint(0xff234557, 1, false));
            canvas.drawText("持有数量", 12, 159, paint(PALETTE[8], 10, false));
            Paint quantity = paint(PALETTE[1], 16, true);
            String amount = present(balance) ? decimal(balance.value()) : "暂无数据";
            while(quantity.getTextSize()>10 && quantity.measureText(amount)>192)quantity.setTextSize(quantity.getTextSize()-1);
            canvas.drawText(fit(amount,quantity,192),12,181,quantity);
            canvas.drawText("余额 " + shortTime(balance),12,205,paint(PALETTE[9],10,false));
            canvas.drawText("行情 " + shortTime(price),12,222,paint(PALETTE[9],10,false));
            canvas.drawText("完整地址及来源见手机详情",12,244,paint(PALETTE[8],9,false));
            add(pages, previews, bitmap, DevicePage.KIND_ASSET, true);
        }
        return new Rendered(List.copyOf(pages), List.copyOf(previews));
    }

    private static void drawAmount(Canvas canvas, String value, int baseline) {
        Paint font = paint(PALETTE[1], 30, true);
        while(font.getTextSize()>12 && font.measureText(value)>192)font.setTextSize(font.getTextSize()-1);
        canvas.drawText(fit(value,font,192),12,baseline,font);
    }
    private static String shortTime(CachedAmount value) {
        return value == null || value.timestamp() == null ? "尚未更新" :
            DateTimeFormatter.ofPattern("MM-dd HH:mm 'UTC'").withZone(ZoneOffset.UTC).format(value.timestamp());
    }

    private static boolean present(CachedAmount amount) { return amount != null && amount.value() != null; }
    private static String decimal(BigDecimal number) { return number.stripTrailingZeros().toPlainString(); }
    private static String money(BigDecimal number) {
        return number.setScale(2, java.math.RoundingMode.HALF_UP).toPlainString();
    }
    private static String time(CachedAmount amount) {
        return amount == null || amount.timestamp() == null ? "never" : TIME.format(amount.timestamp());
    }

    private static void validateGlyphs(WalletBackup wallet) {
        Paint font = paint(Color.BLACK, 14, false);
        List<String> strings = new ArrayList<>();
        strings.add(wallet.profile().nickname());
        strings.add(wallet.profile().bio());
        for (SocialLink social : wallet.socials()) {
            strings.add(social.service()); strings.add(social.handle()); strings.add(social.url());
        }
        for (PaymentCode payment : wallet.payments()) strings.add(payment.label());
        for (WalletEntry entry : wallet.entries()) strings.add(entry.label());
        for (String text : strings) {
            for (int i = 0; i < text.length();) {
                int cp = text.codePointAt(i);
                String glyph = new String(Character.toChars(cp));
                if (!Character.isWhitespace(cp) && !font.hasGlyph(glyph)) {
                    throw new IllegalArgumentException("Device font cannot render U+" +
                        Integer.toHexString(cp).toUpperCase(java.util.Locale.ROOT) + "; replace this character.");
                }
                i += Character.charCount(cp);
            }
        }
    }

    private static Bitmap base() {
        Bitmap bitmap = Bitmap.createBitmap(Pcw1.WIDTH, Pcw1.HEIGHT, Bitmap.Config.ARGB_8888);
        new Canvas(bitmap).drawColor(PALETTE[0]);
        return bitmap;
    }
    private static Paint paint(int color, float size, boolean bold) {
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setColor(color); p.setTextSize(size);
        p.setTypeface(bold ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        return p;
    }
    private static Bitmap card(Profile profile, List<SocialLink> socials) {
        Bitmap bitmap = base();
        Canvas canvas = new Canvas(bitmap);
        canvas.drawText("PERSONAL PASSPORT", 12, 18, paint(PALETTE[2], 10, true));
        if (!profile.avatarBase64().isBlank()) {
            byte[] raw = Base64.decode(profile.avatarBase64(), Base64.DEFAULT);
            if (raw.length > 512 * 1024) throw new IllegalArgumentException("Avatar exceeds 512 KB");
            BitmapFactory.Options options = new BitmapFactory.Options();
            options.inJustDecodeBounds = true;
            BitmapFactory.decodeByteArray(raw, 0, raw.length, options);
            if (options.outWidth < 1 || options.outHeight < 1 ||
                options.outWidth > 4096 || options.outHeight > 4096) {
                throw new IllegalArgumentException("Avatar image is invalid or exceeds 4096x4096");
            }
            options.inSampleSize = 1;
            while (Math.max(options.outWidth, options.outHeight) / options.inSampleSize > 256) {
                options.inSampleSize *= 2;
            }
            options.inJustDecodeBounds = false;
            Bitmap avatar = BitmapFactory.decodeByteArray(raw, 0, raw.length, options);
            if (avatar == null) throw new IllegalArgumentException("Avatar image cannot be decoded");
            int side = Math.min(avatar.getWidth(), avatar.getHeight());
            canvas.drawBitmap(avatar,
                new Rect((avatar.getWidth() - side) / 2, (avatar.getHeight() - side) / 2,
                         (avatar.getWidth() + side) / 2, (avatar.getHeight() + side) / 2),
                new Rect(12, 33, 68, 89), paint(Color.WHITE, 1, false));
            avatar.recycle();
        }
        if (profile.avatarBase64().isBlank()) {
            canvas.drawRoundRect(12, 33, 68, 89, 10, 10, paint(0xff234557, 1, false));
            canvas.drawCircle(40, 52, 9, paint(PALETTE[8], 1, false));
            canvas.drawRoundRect(25, 65, 55, 83, 8, 8, paint(PALETTE[8], 1, false));
        }
        Paint name = paint(PALETTE[1], 22, true);
        String shown = profile.nickname().isBlank() ? "你的名片" : profile.nickname();
        while (name.getTextSize() > 14 && name.measureText(shown) > 120) name.setTextSize(name.getTextSize() - 1);
        canvas.drawText("HELLO, I'M", 80, 49, paint(PALETTE[8], 8, false));
        canvas.drawText(fit(shown, name, 124), 80, 75, name);
        canvas.drawText(fit(profile.bio().isBlank() ? "添加一句介绍，让相识更简单" : profile.bio(),
                paint(PALETTE[9], 11, false), 192), 12, 112, paint(PALETTE[9], 11, false));
        for (int i = 0; i < Math.min(3, socials.size()); i++) {
            SocialLink social = socials.get(i);
            int y = 137 + i * 30;
            canvas.drawRect(12, y - 10, 204, y - 9, paint(0xff234557, 1, false));
            canvas.drawText(fit(social.service(), paint(PALETTE[8], 11, false), 57),
                    12, y + 8, paint(PALETTE[8], 11, false));
            Paint handle = paint(PALETTE[1], 11, false);
            String value = fit(social.handle(), handle, 125);
            canvas.drawText(value, 204 - handle.measureText(value), y + 8, handle);
        }
        if (socials.isEmpty()) canvas.drawText("在手机上添加常用社交账号", 12, 159, paint(PALETTE[8], 11, false));
        canvas.drawRoundRect(12, 222, 204, 250, 6, 6, paint(0xff17484c, 1, false));
        canvas.drawText("确认 · 查看完整名片", 24, 241, paint(PALETTE[2], 12, false));
        return bitmap;
    }

    /** Wrapping uses measured glyph advances and retains every input code point. */
    private static List<String> lines(String text, Paint font, float width) {
        List<String> lines = new ArrayList<>();
        for (String paragraph : text.split("\n", -1)) {
            if (paragraph.isEmpty()) { lines.add(""); continue; }
            int offset = 0;
            while (offset < paragraph.length()) {
                int length = font.breakText(paragraph, offset, paragraph.length(), true, width, null);
                if (length > 0 && offset + length < paragraph.length() &&
                    Character.isHighSurrogate(paragraph.charAt(offset + length - 1))) length--;
                if (length <= 0) throw new IllegalArgumentException("A character is too wide for the device page");
                lines.add(paragraph.substring(offset, offset + length));
                offset += length;
            }
        }
        return lines;
    }

    private static void addText(List<DevicePage> pages, List<Bitmap> previews, String title,
                                String text, int kind, boolean privatePage) {
        Paint font = paint(PALETTE[1], 13, false);
        List<String> rows = lines(text, font, 196);
        for (int start = 0; start < rows.size(); start += BODY_LINES) {
            Bitmap bitmap = base(); Canvas canvas = new Canvas(bitmap);
            Paint titleFont = paint(PALETTE[2], 14, true);
            String heading = title + (rows.size() > BODY_LINES ? " " + (start / BODY_LINES + 1) : "");
            canvas.drawText(fit(heading, titleFont, 196), 10, 25, titleFont);
            canvas.drawRect(10, 34, 206, 37, paint(PALETTE[2], 1, false));
            for (int i = start; i < Math.min(start + BODY_LINES, rows.size()); i++) {
                canvas.drawText(rows.get(i), 10, 57 + (i - start) * 18, font);
            }
            add(pages, previews, bitmap, kind, privatePage);
        }
    }

    private static Bitmap qrPage(String title, String payload, String footer) {
        try {
            BitMatrix matrix = QrMatrix.encode(payload);
            PageRules.QrLayout layout = PageRules.qrLayout(matrix.getWidth() - 8, 184);
            int scale = layout.scale(), pixels = layout.pixels();
            Bitmap bitmap = base(); Canvas canvas = new Canvas(bitmap);
            Paint titleFont = paint(PALETTE[1], 14, true);
            canvas.drawText(fit(title, titleFont, 196), 10, 22, titleFont);
            int left = (216 - pixels) / 2, top = 40;
            Paint black = paint(Color.BLACK, 1, false), white = paint(Color.WHITE, 1, false);
            black.setAntiAlias(false); white.setAntiAlias(false);
            canvas.drawRect(left, top, left + pixels, top + pixels, white);
            for (int y = 0; y < matrix.getHeight(); y++) {
                for (int x = 0; x < matrix.getWidth(); x++) {
                    if (matrix.get(x, y)) canvas.drawRect(left + x * scale, top + y * scale,
                        left + (x + 1) * scale, top + (y + 1) * scale, black);
                }
            }
            Paint footerFont = paint(PALETTE[2], 11, true);
            canvas.drawText(fit(footer, footerFont, 196), 10, 248, footerFont);
            return bitmap;
        } catch (Exception error) {
            throw new IllegalArgumentException("Cannot render QR: " + error.getMessage(), error);
        }
    }

    private static String fit(String text, Paint font, float width) {
        if (font.measureText(text) <= width) return text;
        float room = width - font.measureText("…");
        int end = font.breakText(text, true, room, null);
        if (end > 0 && Character.isHighSurrogate(text.charAt(end - 1))) end--;
        return text.substring(0, end) + "…";
    }

    private static void add(List<DevicePage> pages, List<Bitmap> previews, Bitmap bitmap,
                            int kind, boolean privatePage) {
        if (pages.size() >= Pcw1.MAX_PAGES) {
            bitmap.recycle();
            throw new IllegalArgumentException("内容超过设备 52 页容量；请减少资产、账号或缩短文字后重试（不会截断内容）。");
        }
        DevicePage packed = pack(bitmap, kind, privatePage);
        pages.add(packed);
        // Preview the real quantized pixels, not the richer source artwork.
        Bitmap preview = Bitmap.createBitmap(216, 256, Bitmap.Config.ARGB_8888);
        for (int y = 0; y < 256; y++) {
            for (int x = 0; x < 216; x++) preview.setPixel(x, y, PALETTE[nearest(bitmap.getPixel(x, y))]);
        }
        bitmap.recycle();
        previews.add(preview);
    }

    public static DevicePage pack(Bitmap bitmap, int kind, boolean privatePage) {
        if (bitmap.getWidth() != 216 || bitmap.getHeight() != 256) {
            throw new IllegalArgumentException("Preview must be 216x256");
        }
        byte[] pixels = new byte[Pcw1.PAGE_PIXEL_BYTES];
        for (int y = 0; y < 256; y++) {
            for (int x = 0; x < 216; x++) {
                int index = nearest(bitmap.getPixel(x, y)), offset = y * 108 + x / 2;
                if ((x & 1) == 0) pixels[offset] = (byte) (index << 4);
                else pixels[offset] |= (byte) index;
            }
        }
        return new DevicePage(kind, privatePage, PALETTE, pixels);
    }

    private static int nearest(int color) {
        int best = 0, distance = Integer.MAX_VALUE;
        for (int i = 0; i < PALETTE.length; i++) {
            int palette = PALETTE[i];
            int red = Color.red(color) - Color.red(palette);
            int green = Color.green(color) - Color.green(palette);
            int blue = Color.blue(color) - Color.blue(palette);
            int candidate = red * red + green * green + blue * blue;
            if (candidate < distance) { distance = candidate; best = i; }
        }
        return best;
    }
}
