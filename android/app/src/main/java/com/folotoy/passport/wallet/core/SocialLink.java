package com.folotoy.passport.wallet.core;

public record SocialLink(String service,String handle,String url){
    public SocialLink { if(service==null||handle==null||url==null||service.length()>40||handle.length()>120||url.length()>500) throw new IllegalArgumentException("Invalid social link"); }
}
