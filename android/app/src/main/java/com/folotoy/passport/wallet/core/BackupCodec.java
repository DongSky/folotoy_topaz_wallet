package com.folotoy.passport.wallet.core;

import java.util.ArrayList;
import java.util.Base64;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import org.json.JSONArray;
import org.json.JSONObject;
import com.google.gson.Strictness;
import com.google.gson.stream.JsonReader;
import com.google.gson.stream.JsonToken;

public final class BackupCodec {
    private static final int MAX_JSON=2_000_000,MAX_AVATAR_BYTES=512*1024;
    private BackupCodec(){}
    public static String encode(WalletBackup w){
        try {
        JSONObject root=new JSONObject();root.put("version",w.version()).put("preferredCurrency",w.preferredCurrency());
        root.put("profile",new JSONObject().put("nickname",w.profile().nickname()).put("bio",w.profile().bio()).put("avatarBase64",w.profile().avatarBase64()));
        JSONArray socials=new JSONArray();for(SocialLink s:w.socials())socials.put(new JSONObject().put("service",s.service()).put("handle",s.handle()).put("url",s.url()));root.put("socials",socials);
        JSONArray payments=new JSONArray();for(PaymentCode p:w.payments())payments.put(new JSONObject().put("label",p.label()).put("payload",p.payload()));root.put("payments",payments);
        JSONArray entries=new JSONArray();for(WalletEntry e:w.entries())entries.put(new JSONObject().put("network",e.network().id()).put("address",e.address()).put("asset",e.asset()).put("label",e.label()));root.put("entries",entries);return root.toString(2);
        } catch (Exception e) { throw new IllegalArgumentException("Could not encode backup", e); }
    }
    public static WalletBackup decode(String json){
        try{if(json==null||json.length()>MAX_JSON)throw new IllegalArgumentException("Backup missing or exceeds 2 MB");validateStrict(json);JSONObject root=new JSONObject(json);keys(root,"version","preferredCurrency","profile","socials","payments","entries");if(!(root.get("version") instanceof Integer))throw new IllegalArgumentException("Backup version must be an integer");int version=root.getInt("version");if(version!=WalletBackup.SCHEMA_VERSION)throw new IllegalArgumentException("Unsupported backup version: "+version);String currency=string(root,"preferredCurrency");
            JSONObject p=root.getJSONObject("profile");keys(p,"nickname","bio","avatarBase64");String avatar=string(p,"avatarBase64");if(!avatar.isEmpty()){byte[] decoded=Base64.getDecoder().decode(avatar);if(decoded.length>MAX_AVATAR_BYTES)throw new IllegalArgumentException("Avatar exceeds 512 KB");}
            Profile profile=new Profile(string(p,"nickname"),string(p,"bio"),avatar);JSONArray sa=root.getJSONArray("socials"),pa=root.getJSONArray("payments"),ea=root.getJSONArray("entries");if(sa.length()>64||pa.length()>64||ea.length()>64)throw new IllegalArgumentException("Backup item limit exceeded");
            List<SocialLink> socials=new ArrayList<>();for(int i=0;i<sa.length();i++){JSONObject x=sa.getJSONObject(i);keys(x,"service","handle","url");socials.add(new SocialLink(string(x,"service"),string(x,"handle"),string(x,"url")));}
            List<PaymentCode> payments=new ArrayList<>();for(int i=0;i<pa.length();i++){JSONObject x=pa.getJSONObject(i);keys(x,"label","payload");payments.add(new PaymentCode(string(x,"label"),string(x,"payload")));}
            List<WalletEntry> entries=new ArrayList<>();for(int i=0;i<ea.length();i++){JSONObject x=ea.getJSONObject(i);keys(x,"network","address","asset","label");entries.add(new WalletEntry(Network.fromId(string(x,"network")),string(x,"address"),string(x,"asset"),string(x,"label")));}
            return new WalletBackup(version,profile,socials,payments,entries,currency);
        }catch(IllegalArgumentException e){throw e;}catch(Exception e){throw new IllegalArgumentException("Malformed backup: "+e.getMessage(),e);}
    }
    private static void keys(JSONObject o,String...allowed){Set<String>a=new HashSet<>(List.of(allowed));for(java.util.Iterator<String> it=o.keys();it.hasNext();){String k=it.next();if(!a.remove(k))throw new IllegalArgumentException("Unexpected backup field: "+k);}if(!a.isEmpty())throw new IllegalArgumentException("Missing backup field: "+a.iterator().next());}
    private static String string(JSONObject o,String key)throws Exception{Object v=o.get(key);if(!(v instanceof String))throw new IllegalArgumentException("Backup field must be a string: "+key);return (String)v;}
    private static void validateStrict(String json)throws Exception{JsonReader r=new JsonReader(new java.io.StringReader(json));r.setStrictness(Strictness.STRICT);readValue(r,0);if(r.peek()!=JsonToken.END_DOCUMENT)throw new IllegalArgumentException("Trailing backup content");}
    private static void readValue(JsonReader r,int depth)throws Exception{if(depth>16)throw new IllegalArgumentException("Backup JSON nesting exceeds 16 levels");switch(r.peek()){case BEGIN_OBJECT->{r.beginObject();Set<String> names=new HashSet<>();while(r.hasNext()){String n=r.nextName();if(!names.add(n))throw new IllegalArgumentException("Duplicate backup field: "+n);readValue(r,depth+1);}r.endObject();}case BEGIN_ARRAY->{r.beginArray();while(r.hasNext())readValue(r,depth+1);r.endArray();}case STRING->r.nextString();case NUMBER->r.nextString();case BOOLEAN->r.nextBoolean();case NULL->r.nextNull();default->throw new IllegalArgumentException("Malformed backup JSON");}}
}
