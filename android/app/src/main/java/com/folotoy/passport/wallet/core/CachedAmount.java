package com.folotoy.passport.wallet.core;

import java.math.BigDecimal;
import java.time.Instant;

public record CachedAmount(BigDecimal value, Instant timestamp, String error) {
    public static CachedAmount success(BigDecimal value, Instant timestamp) { return new CachedAmount(value, timestamp, null); }
    public CachedAmount failureKeepingValue(String message) { return new CachedAmount(value, timestamp, message); }
    public boolean missing() { return value == null || timestamp == null; }
    public boolean stale(Instant now, long maxAgeSeconds) { return missing() || timestamp.plusSeconds(maxAgeSeconds).isBefore(now); }
}
