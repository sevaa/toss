package app.tosstoself.toss;

import android.app.Dialog;
import android.content.Context;
import android.widget.TextView;

public class AboutDlg extends Dialog
{
    public AboutDlg(Context Ctxt)
    {
        super(Ctxt);
        setContentView(getLayoutInflater().inflate(R.layout.about, null));

        setTitle(Ctxt.getString(R.string.IDS_ABOUT));
        String version = Util.safely(() -> Ctxt.getPackageManager().getPackageInfo(Ctxt.getPackageName(), 0).versionName);
        ((TextView)findViewById(R.id.Version)).setText(String.format(Ctxt.getString(R.string.IDS_ABOUT_VER), version));
    }
}
