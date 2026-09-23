package com.folotoy.passport.wallet.data;

import android.content.Context;
import android.content.SharedPreferences;
import com.folotoy.passport.wallet.core.*;
import java.math.BigDecimal;
import java.time.Instant;
import java.util.HashMap;
import java.util.Map;

public final class WalletRepository {
    private static final String PREFS="wallet_private_v1",BACKUP="backup",CACHE="cache";
    private static final Object WRITE_LOCK = new Object();
    private final SharedPreferences prefs;
    public WalletRepository(Context c){prefs=c.getSharedPreferences(PREFS,Context.MODE_PRIVATE);}
    public WalletBackup load(){String s=prefs.getString(BACKUP,null);if(s==null)return new WalletBackup(1,new Profile("","",""),java.util.List.of(),java.util.List.of(),java.util.List.of());try{return BackupCodec.decode(s);}catch(Exception e){throw new IllegalStateException("Stored wallet is damaged. It was preserved; import a valid backup to recover.",e);}}
    public void save(WalletBackup b){synchronized(WRITE_LOCK){if(!prefs.edit().putString(BACKUP,BackupCodec.encode(b)).commit())throw new IllegalStateException("Could not save wallet");}}
    /** Compare and commit as one operation across Activity/repository instances. */
    public boolean compareAndSave(WalletBackup expected, WalletBackup next) {
        synchronized(WRITE_LOCK) { if(!load().equals(expected))return false;save(next);return true; }
    }
    /** Derivation can finish after recreation; merge against the latest public data atomically. */
    public void addEntries(java.util.List<WalletEntry> additions) {
        synchronized(WRITE_LOCK) {
            WalletBackup current=load();
            java.util.ArrayList<WalletEntry> merged=new java.util.ArrayList<>(current.entries());
            merged.addAll(additions);
            save(new WalletBackup(current.version(),current.profile(),current.socials(),current.payments(),merged,current.preferredCurrency()));
        }
    }
    public Map<String,CachedAmount> cache(){Map<String,CachedAmount> out=new HashMap<>();String all=prefs.getString(CACHE,"");for(String line:all.split("\\n")){String[] p=line.split("\\|",4);if(p.length==4)try{out.put(p[0],new CachedAmount(p[1].isEmpty()?null:new BigDecimal(p[1]),p[2].isEmpty()?null:Instant.ofEpochMilli(Long.parseLong(p[2])),p[3].isEmpty()?null:p[3]));}catch(Exception ignored){}}return out;}
    public void saveCache(Map<String,CachedAmount> c){StringBuilder b=new StringBuilder();for(var e:c.entrySet()){CachedAmount a=e.getValue();if(e.getKey().contains("|")||(a.error()!=null&&(a.error().contains("|")||a.error().contains("\n"))))continue;b.append(e.getKey()).append('|').append(a.value()==null?"":a.value().toPlainString()).append('|').append(a.timestamp()==null?"":a.timestamp().toEpochMilli()).append('|').append(a.error()==null?"":a.error()).append('\n');}if(!prefs.edit().putString(CACHE,b.toString()).commit())throw new IllegalStateException("Could not save cache");}
}
