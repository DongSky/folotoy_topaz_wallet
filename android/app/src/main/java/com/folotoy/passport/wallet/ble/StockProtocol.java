package com.folotoy.passport.wallet.ble;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;

/** Stock 1.0.3 command bytes from static instruction evidence, not device acceptance.
 * Only profile text and image commands are exposed; no wallet accounts or secrets.
 */
public final class StockProtocol {
    public static final UUID SERVICE=UUID.fromString("54524145-4341-5244-0000-000000000000"),
        WRITE=UUID.fromString("54524145-4341-5244-0000-000000000010"),
        RESPONSE=UUID.fromString("54524145-4341-5244-0000-000000000011");
    public static final int PAYLOAD_MAX=256, IMAGE_CHUNK_MAX=255;
    public enum ImageMode { AVATAR, FULLSCREEN }
    public static final class Command {
        private final int type,successStatus;
        private final byte[] frame;
        private Command(int type,int successStatus,byte[] payload) {
            this.type=type;this.successStatus=successStatus;this.frame=encode(type,payload);
        }
        public int type(){return type;}
        public int successStatus(){return successStatus;}
        public byte[] frame(){return frame.clone();}
    }
    private StockProtocol(){}

    /** Separate acknowledged fields avoid stock's 256-byte JSON limit. No truncation. */
    public static List<Command> profile(String nickname,String intro) {
        validateText(nickname,47,Integer.MAX_VALUE,"Nickname");
        validateText(intro,84,28,"Introduction");
        return List.of(json("nickname",nickname),json("intro",intro));
    }
    /** Caller prepares JPEG dimensions (avatar96x160/fullscreen240x320) and chooses
     * a deliberate application limit. Actual stock partition capacity is not inferred.
     * END success acknowledges storage, not JPEG decoding or display.
     */
    public static List<Command> image(ImageMode mode,byte[] jpeg,int maxBytes) {
        if(mode==null||jpeg==null||jpeg.length==0||maxBytes<=0||jpeg.length>maxBytes)
            throw new IllegalArgumentException("Image is missing or exceeds configured capacity");
        if(jpeg.length<4||(jpeg[0]&255)!=0xff||(jpeg[1]&255)!=0xd8
                ||(jpeg[jpeg.length-2]&255)!=0xff||(jpeg[jpeg.length-1]&255)!=0xd9)
            throw new IllegalArgumentException("JPEG encoding is required");
        List<Command> commands=new ArrayList<>();
        commands.add(json("img_mode",mode==ImageMode.AVATAR?"avatar":"fullscreen"));
        int total=jpeg.length;
        commands.add(new Command(2,0,new byte[]{0,(byte)total,(byte)(total>>>8),(byte)(total>>>16),(byte)(total>>>24)}));
        for(int offset=0;offset<total;){
            int count=Math.min(IMAGE_CHUNK_MAX,total-offset);byte[] data=new byte[count+1];data[0]=1;
            System.arraycopy(jpeg,offset,data,1,count);commands.add(new Command(2,0,data));offset+=count;
        }
        commands.add(new Command(2,0,new byte[]{2}));return List.copyOf(commands);
    }
    private static Command json(String key,String value) {
        byte[] payload=("{\""+key+"\":"+quote(value)+"}").getBytes(StandardCharsets.UTF_8);
        return new Command(1,1,payload);
    }
    private static void validateText(String value,int maxBytes,int maxPoints,String label) {
        if(value==null)throw new IllegalArgumentException(label+" is required");
        for(int i=0;i<value.length();i++){
            char c=value.charAt(i);
            if(c==0)throw new IllegalArgumentException(label+" contains NUL");
            if(Character.isHighSurrogate(c)){
                if(i+1>=value.length()||!Character.isLowSurrogate(value.charAt(++i)))throw new IllegalArgumentException(label+" contains malformed Unicode");
            }else if(Character.isLowSurrogate(c))throw new IllegalArgumentException(label+" contains malformed Unicode");
        }
        if(value.getBytes(StandardCharsets.UTF_8).length>maxBytes||value.codePointCount(0,value.length())>maxPoints)
            throw new IllegalArgumentException(label+" exceeds stock text capacity");
    }
    private static String quote(String value) {
        StringBuilder b=new StringBuilder("\"");
        for(int i=0;i<value.length();i++){
            char c=value.charAt(i);
            if(c=='"'||c=='\\')b.append('\\').append(c);
            else if(c<32){b.append("\\u00").append(Character.forDigit(c>>>4,16)).append(Character.forDigit(c&15,16));}
            else b.append(c);
        }
        return b.append('"').toString();
    }
    public static byte[] encode(int type,byte[] payload) {
        if(type<0||type>255||payload==null||payload.length>PAYLOAD_MAX)throw new IllegalArgumentException("Invalid stock frame");
        byte[] frame=new byte[payload.length+4];frame[0]=1;frame[1]=(byte)type;
        frame[2]=(byte)payload.length;frame[3]=(byte)(payload.length>>>8);System.arraycopy(payload,0,frame,4,payload.length);return frame;
    }
    /** One list per logical frame. NEVER concatenate neighboring frames in an ATT write. */
    public static List<byte[]> fragments(Command command,int mtu) {
        if(command==null||mtu<23||mtu>517)throw new IllegalArgumentException("ATT MTU must be 23..517");
        byte[] frame=command.frame;int capacity=mtu-3;List<byte[]> parts=new ArrayList<>();
        for(int offset=0;offset<frame.length;offset+=capacity)parts.add(Arrays.copyOfRange(frame,offset,Math.min(frame.length,offset+capacity)));
        return parts;
    }
}
