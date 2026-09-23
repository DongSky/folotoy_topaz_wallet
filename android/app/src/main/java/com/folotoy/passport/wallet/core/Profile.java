package com.folotoy.passport.wallet.core;

public record Profile(String nickname, String bio, String avatarBase64) {
    public Profile { if(nickname==null||bio==null||avatarBase64==null) throw new IllegalArgumentException("Profile fields required"); if(nickname.length()>80||bio.length()>800) throw new IllegalArgumentException("Profile text too long"); }
}
