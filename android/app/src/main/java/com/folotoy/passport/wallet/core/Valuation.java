package com.folotoy.passport.wallet.core;

import java.math.BigDecimal;
import java.time.Instant;
import java.util.List;
import java.util.Map;

public final class Valuation {
    public record Result(BigDecimal total, boolean incomplete, int includedEntries) {}
    private Valuation() {}
    public static Result total(List<WalletEntry> entries, Map<String,CachedAmount> balances,
                               Map<String,CachedAmount> prices, String currency, Instant now, long maxAgeSeconds) {
        BigDecimal total = BigDecimal.ZERO; boolean incomplete=false; int included=0;
        for (WalletEntry e : WalletEntries.deduplicate(entries)) {
            CachedAmount b=balances.get(e.key()), p=prices.get(e.priceKey(currency));
            if (b==null || p==null || b.missing() || p.missing()) { incomplete=true; continue; }
            total=total.add(b.value().multiply(p.value())); included++;
            if (b.stale(now,maxAgeSeconds) || p.stale(now,maxAgeSeconds) || b.error()!=null || p.error()!=null) incomplete=true;
        }
        return new Result(total, incomplete, included);
    }
}
