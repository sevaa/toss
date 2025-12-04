package app.tosstoself.toss;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.content.Intent;
import android.text.ClipboardManager;
import android.text.Selection;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;

public class MessageDlg extends Dialog
{
    private static class Message
    {
        public String From, Text;
    }

    private ArrayList<Message> m_Queue = new ArrayList<>();

    public MessageDlg(Activity a, String From, String Text)
    {
        super(a);
        setContentView(getLayoutInflater().inflate(R.layout.message, null));
        ((TextView)findViewById(R.id.TheText)).setText(Text);
        setTitle(String.format(a.getString(R.string.IDS_PEERSAYS), From));

        findViewById(R.id.Copy).setOnClickListener((View v) -> onCopy());
        findViewById(R.id.Share).setOnClickListener((View v) -> onShare());
        findViewById(R.id.Next).setOnClickListener((View v) -> onNext());
    }

    public void onConfigurationChanged(boolean bLand)
    {
        ((LinearLayout)findViewById(R.id.Main)).setOrientation(bLand ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);
        ((LinearLayout)findViewById(R.id.BuPanel)).setOrientation(bLand ? LinearLayout.VERTICAL : LinearLayout.HORIZONTAL);
    }

    public void queueMessage(String From, String Text)
    {
        Message m = new Message();
        m.From = From;
        m.Text = Text;
        m_Queue.add(m);
        findViewById(R.id.Next).setEnabled(true);
    }

    private void onCopy()
    {
        ClipboardManager cm = (ClipboardManager)getContext().getSystemService(Context.CLIPBOARD_SERVICE);
        CharSequence cs = ((TextView)findViewById(R.id.TheText)).getText();
        int Start = Selection.getSelectionStart(cs), End = Selection.getSelectionEnd(cs);
        if(Start >= 0 && End >= 0 && Start < End)
            cs = cs.subSequence(Start, End);

        cm.setText(cs.toString());
        Toast.makeText(getContext(), R.string.IDS_COPIED, Toast.LENGTH_SHORT).show();
    }

    private void onShare()
    {
        String Text = ((TextView)findViewById(R.id.TheText)).getText().toString(),
            Title = getContext().getString(R.string.IDS_TOSSEDMSG);
        getContext().startActivity(
            Intent.createChooser(
                new Intent(Intent.ACTION_SEND)
                    .setType("text/plain")
                    .putExtra(Intent.EXTRA_TEXT, Text), Title));
    }

    private void onNext()
    {
        Message m = m_Queue.get(0);
        m_Queue.remove(0);

        setTitle(String.format(getContext().getString(R.string.IDS_PEERSAYS), m.From));
        ((TextView)findViewById(R.id.TheText)).setText(m.Text);
        findViewById(R.id.Next).setEnabled(m_Queue.size() > 0);
    }
}
