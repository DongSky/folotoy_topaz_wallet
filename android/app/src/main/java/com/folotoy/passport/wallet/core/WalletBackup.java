package com.folotoy.passport.wallet.core;

import java.util.List;

public record WalletBackup(int version, Profile profile, List<SocialLink> socials, List<PaymentCode> payments, List<WalletEntry> entries, String preferredCurrency) {
    public static final int SCHEMA_VERSION=1;
    public WalletBackup { if(version!=SCHEMA_VERSION) throw new IllegalArgumentException("Unsupported backup version"); socials=List.copyOf(socials);payments=List.copyOf(payments);entries=List.copyOf(WalletEntries.deduplicate(entries)); if(socials.size()>64||payments.size()>64||entries.size()>64) throw new IllegalArgumentException("Backup item limit exceeded"); if(!List.of("USD","CNY").contains(preferredCurrency))throw new IllegalArgumentException("Currency must be USD or CNY"); }
    public WalletBackup(int version,Profile profile,List<SocialLink> socials,List<PaymentCode> payments,List<WalletEntry> entries){this(version,profile,socials,payments,entries,"USD");}
}
