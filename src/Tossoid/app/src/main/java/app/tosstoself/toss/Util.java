package app.tosstoself.toss;

import android.content.Context;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;
import android.util.Base64;

import java.util.Arrays;
import java.util.UUID;

public class Util
{
    public interface Func0<T>
    {
        T call() throws Exception;
    }

    public static SharedPreferences prefs(Context Ctxt)
    {
        return PreferenceManager.getDefaultSharedPreferences(Ctxt);
    }

    public static <T> T safely(Func0<T> f)
    {
        try
        {
            return f.call();
        }
        catch(Exception exc)
        {
            return null;
        }
    }

    public static void safely(Runnable f)
    {
        try { f.run(); }
        catch(Exception exc){ }
    }

    public static void GUIDToBytes(UUID g, byte[]b, int offset)
    {
        //The order of bits is little endian - Windows has to adapt.
        int i;
        long hi = g.getMostSignificantBits(), lo = g.getLeastSignificantBits();
        for(i=0;i<8;i++)
        {
            b[offset + i] = (byte) (hi >> (56 - 8*i));
            b[offset + 8 + i] = (byte) (lo >> (56 - 8*i));
        }
    }

    public static byte[] GUIDToBytes(UUID g)
    {
        byte []b = new byte[16];
        GUIDToBytes(g, b, 0);
        return b;
    }

    public static String GUIDToBase64(UUID g)
    {
        return Base64.encodeToString(GUIDToBytes(g), Base64.URL_SAFE|Base64.NO_PADDING|Base64.NO_WRAP);
    }

    public static UUID bytesToGUID(byte []b, int offset)
    {
        int i;
        long hi = 0, lo = 0;
        for(i=0;i<8;i++)
        {
            hi |= (((long)b[offset + i]) & 0xff) << (56 - 8*i);
            lo |= (((long)b[offset + 8 + i]) & 0xff) << (56 - 8*i);
        }
        return new UUID(hi, lo);
    }

    public static UUID Base64ToGUID(String s)
    {
        return bytesToGUID(Base64.decode(s, Base64.URL_SAFE|Base64.NO_PADDING), 0);
    }

    //Assumes the byte buffer is correct UTF8
    public static byte[] cutUTF8Bytes(byte []b, int n)
    {
        if(b.length <= n)
            return b;
        else //Cut needed
        {
            while((byte)(b[n] & 0xC0) == (byte)0x80)
                --n;
            return Arrays.copyOf(b, n);
        }
    }
}
