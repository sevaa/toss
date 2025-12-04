package app.tosstoself.toss;

import android.app.Activity;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;

import java.io.IOException;
import java.net.BindException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.DatagramChannel;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentLinkedQueue;

public class Main extends Activity
        implements NsdManager.DiscoveryListener,
        AdapterView.OnItemClickListener,
        TextWatcher,
        DialogInterface.OnDismissListener
{
    static private final int V0_DATAGRAM_SIZE = 512,
            V0_HEADER_SIZE = 4+16; //Version+GUID

    private static UUID s_MyGUID;
    static private NsdManager s_Mgr = null;
    private final ArrayList<NsdServiceInfo> m_ResolveQueue = new ArrayList<NsdServiceInfo>();
    private boolean m_Resolving = false;
    private ArrayAdapter<Peer> m_Ada;
    private final RegListener m_RegLis = new RegListener();
    private int m_SelIndex = -1;
    private static final ByteBuffer s_ReceiveGram = ByteBuffer.allocate(V0_DATAGRAM_SIZE);
    private MessageDlg m_MsgDlg = null;
    private DatagramChannel m_Channel;
    private Selector m_Sel;
    private final ConcurrentLinkedQueue<OutgoingMessage> m_SendQueue = new ConcurrentLinkedQueue<OutgoingMessage>();

    static private class OutgoingMessage
    {
        public ByteBuffer data;
        public InetSocketAddress to;
    }

    static public class Peer
    {
        private String m_Name;
        private int m_Port;
        private InetAddress m_Addr;
        private UUID m_GUID;

        public Peer(String n, InetAddress h, int p, UUID g)
        {
            m_Name = n;
            m_Port = p;
            m_Addr = h;
            m_GUID = g;
        }

        // For deleting entries if NSD reports loss
        public Boolean equals(NsdServiceInfo si)
        {
            return si.getHost() == m_Addr && si.getPort() == m_Port;
        }

        public OutgoingMessage buildMessage(byte []d)
        {
            OutgoingMessage om = new OutgoingMessage();
            om.to = new InetSocketAddress(m_Addr, m_Port);
            om.data = ByteBuffer.wrap(d);
            return om;
        }

        public String name() { return m_Name; }

        public UUID GUID() { return m_GUID; }

        public String toString()
        {
            return m_Name;
        }

        public void cameFrom(InetAddress a, int p)
        {
            m_Addr = a;
            m_Port = p;
        }
    }

    //region Activity lifetime
    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        ensureMyGUID();
        if(s_Mgr == null)
            s_Mgr = (NsdManager)getSystemService(Context.NSD_SERVICE);

        //Start listening
        String advName = Build.MODEL;
        try
        {
            SharedPreferences Prefs = Util.prefs(this);
            int port = Prefs.getInt("Port", 0);
            m_Channel = DatagramChannel.open();
            if (port != 0)
            {
                try
                {
                    m_Channel.bind(new InetSocketAddress(port));
                }
                catch(BindException exc)
                { //No numeric error code, we assume all realistic BindExceptions are EADDRINUSE :(
                    port = 0; // Preferred port not available, find a new one
                }
            }

            if(port == 0) //No preferred port or unavailable
            {
                m_Channel.bind(null);
                port = m_Channel.socket().getLocalPort();
                Prefs.edit().putInt("Port", port).apply();
            }

            m_Channel.configureBlocking(false);
            m_Sel = Selector.open();
            m_Channel.register(m_Sel, SelectionKey.OP_READ);
            startReceiving();

            //Register self
            NsdServiceInfo si = new NsdServiceInfo();
            si.setServiceType("_toss._udp.");

            String BTName = Util.safely(() -> Settings.Secure.getString(getContentResolver(), "bluetooth_name"));

            si.setServiceName(Build.MODEL);
            si.setPort(port);
            si.setAttribute("GUID", Util.GUIDToBase64(s_MyGUID));
            if(BTName != null)
            {
                si.setAttribute("Name", BTName);
                advName = BTName;
            }
            s_Mgr.registerService(si, NsdManager.PROTOCOL_DNS_SD, m_RegLis);
        }
        catch(IOException exc)
        {
            android.util.Log.d("Toss", "Init exc", exc);
            finish();
        }

        // Set up the view
        Intent in = getIntent();
        setContentView(getLayoutInflater().inflate(R.layout.main, null));
        ((TextView)findViewById(R.id.AdvName)).setText(String.format(getString(R.string.IDS_KNOWNAS), advName));
        m_Ada = new ArrayAdapter<Peer>(this, android.R.layout.simple_list_item_1);
        ListView TheList = findViewById(R.id.TheList);
        TheList.setOnItemClickListener(this);
        TheList.setAdapter(m_Ada);
        TextView tv = new TextView(this);
        tv.setText(R.string.IDS_NONEFOUND);
        TheList.setEmptyView(tv);
        findViewById(R.id.Send).setOnClickListener((View v) -> onSend());
        EditText et = findViewById(R.id.TheText);
        et.setOnContextClickListener((View v) -> {onSend(); return true;});
        et.addTextChangedListener(this);
        et.setText(in.getStringExtra(Intent.EXTRA_TEXT)); // Tossed text, if any
        findViewById(R.id.Clear).setOnClickListener((View v) -> onClear());
    }

    private void ensureMyGUID()
    {
        SharedPreferences Prefs = Util.prefs(this);
        String s = Prefs.getString("MyGUID", null);
        if(s == null)
        {
            s_MyGUID = UUID.randomUUID(); //TODO: v2 or v6 UUID
            Prefs.edit().putString("MyGUID", s_MyGUID.toString()).commit();
        }
        else
            s_MyGUID = UUID.fromString(s);
    }

    @Override
    protected void onResume()
    {
        super.onResume();
        s_Mgr.discoverServices("_toss._udp.", NsdManager.PROTOCOL_DNS_SD, this);
    }

    @Override
    protected void onPause()
    {
        s_Mgr.stopServiceDiscovery(this);
        super.onPause();
    }

    @Override
    protected void onDestroy()
    {
        if(s_Mgr != null)
            try
            {
                s_Mgr.unregisterService(m_RegLis);
            }
            catch(Exception exc){}

        if(m_Channel != null)
        {
            try
            {
                m_Channel.close();
                m_Channel = null;
            }
            catch (Exception exc)
            {
            }
        }

        super.onDestroy();
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig)
    {
        super.onConfigurationChanged(newConfig);
        Boolean bLand = newConfig.orientation == Configuration.ORIENTATION_LANDSCAPE;
        ((LinearLayout)findViewById(R.id.Main)).setOrientation(bLand ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);
        LinearLayout.LayoutParams lpl = (LinearLayout.LayoutParams)((LinearLayout)findViewById(R.id.ListPanel)).getLayoutParams();
        LinearLayout.LayoutParams lps = (LinearLayout.LayoutParams)((LinearLayout)findViewById(R.id.SendPanel)).getLayoutParams();
        lpl.width = lps.width = bLand ? 0 : LinearLayout.LayoutParams.MATCH_PARENT;
        lpl.height = lps.height = bLand ? LinearLayout.LayoutParams.MATCH_PARENT : 0;

        if(m_MsgDlg != null)
            m_MsgDlg.onConfigurationChanged(bLand);
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu)
    {
        getMenuInflater().inflate(R.menu.main, menu);
        return super.onCreateOptionsMenu(menu);
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item)
    {
        int id = item.getItemId();
        if(id == R.id.About)
            new AboutDlg(this).show();
        return super.onOptionsItemSelected(item);
    }

    //endregion

    //region UI callbacks
    @Override
    public void onItemClick(AdapterView<?> parent, View view, int i, long id)
    {
        m_SelIndex = i;
        Peer se = m_Ada.getItem(i);

        ((Button)findViewById(R.id.Send)).setEnabled(true);
        ((TextView)findViewById(R.id.SendingTo)).setText(String.format(this.getString(R.string.IDS_SENDTO), se.name()));
        EditText et = (EditText)findViewById(R.id.TheText);
        et.setEnabled(true);
        et.requestFocus();
    }

    private void onSend()
    {
        String s = ((EditText) findViewById(R.id.TheText)).getText().toString();
        byte[] Text = s.getBytes(StandardCharsets.UTF_8);
        Text = Util.cutUTF8Bytes(Text, V0_DATAGRAM_SIZE - V0_HEADER_SIZE); //4+16 is v0 header, 512 is max datagram size
        byte[] Data = new byte[V0_HEADER_SIZE + Text.length];
        Data[0] = Data[1] = Data[2] = Data[3] = 0;
        Util.GUIDToBytes(s_MyGUID, Data, 4);
        System.arraycopy(Text, 0, Data, V0_HEADER_SIZE, Text.length);

        final Peer se = m_Ada.getItem(m_SelIndex);
        android.util.Log.d("Send", String.format("Send to %s:%d - %d", se.m_Addr, se.m_Port, Data.length));

        OutgoingMessage om = se.buildMessage(Data);
        m_SendQueue.add(om);
        m_Sel.wakeup();

        /*
        new Thread(() ->
        {
            try
            {
                m_Channel.send(ByteBuffer.wrap(Data), new InetSocketAddress(se.m_Addr, se.m_Port));
                android.util.Log.d("Send", "Sent");

            }
            catch(Exception exc)
            {
                android.util.Log.d("Send", "Boo", exc);
            }
        }).start();
         */


        //m_SendGram = m_Ada.getItem(m_SelIndex).datagramFor(Data);
        //android.util.Log.d("Send", String.format("Send to %s:%d - %d", m_SendGram.getAddress(), m_SendGram.getPort(), Data.length));

        //DatagramSocket Sock = m_Sock;
        //m_Sock = null;
        //Sock.close(); //Force stop receive, socket recreation and repeat listen

        /*
        new Thread(() -> {
            try
            {
                m_Sock.send(dp);

                DatagramSocket Sock = m_Sock;
                m_Sock = null;
                Sock.close(); //Force socket recreation and repeat listen
            }
            catch (IOException exc)
            {
                android.util.Log.d("Send", "Boo", exc);
            }}).start();

         */
    }

    private void onClear()
    {
        ((EditText)findViewById(R.id.TheText)).setText(null);
    }

    @Override
    public void onTextChanged(CharSequence s, int start, int before, int count)
    {
        findViewById(R.id.Clear).setEnabled(s.length() > 0);
    }

    @Override
    public void beforeTextChanged(CharSequence s, int start, int count, int after) { }

    @Override
    public void afterTextChanged(Editable s) { }

    @Override
    public void onDismiss(DialogInterface dialog)
    {
        m_MsgDlg = null;
    }

    //endregion

    //region Discovery

    //UI thread handler of a newly discovered service
    private void displayFoundService(Peer se)
    {
        String nameKey = "NameForGUID_" + se.m_GUID.toString();
        SharedPreferences p = Util.prefs(this);
        String savedName = p.getString(nameKey, null);
        if(!se.name().equals(savedName))
            p.edit().putString(nameKey, se.name()).apply();

        int i, n = m_Ada.getCount();
        for(i=0;i<n;i++)
        {
            Peer lse = m_Ada.getItem(i);
            if(lse.GUID().equals(se.GUID()))
            {
                m_Ada.remove(lse);
                m_Ada.insert(se, i);
                return;
            }
        }
        m_Ada.add(se);
    }

    @Override
    public void onDiscoveryStarted(String serviceType) {}

    public void onDiscoveryStopped(String serviceType)
    {
        android.util.Log.d("Discover", "Stopped");
    }

    @Override
    public void onServiceFound(NsdServiceInfo si)
    {
        android.util.Log.d("Discover", "Found " + si.getServiceName());
        if(m_Resolving)
            m_ResolveQueue.add(si);
        else
        {
            m_Resolving = true;
            resolve(si);
        }
    }

    private void resolveNext()
    {
        if(!m_ResolveQueue.isEmpty())
        {
            resolve(m_ResolveQueue.get(0));
            m_ResolveQueue.remove(0);
        }
        else
            m_Resolving = false;
    }

    private void resolve(NsdServiceInfo si)
    {
        s_Mgr.resolveService(si, new NsdManager.ResolveListener()
        {
            public void onServiceResolved(NsdServiceInfo si)
            {
                android.util.Log.d("Discover", "Resolved " + si.getServiceName());
                Map<String, byte[]> a = si.getAttributes();
                byte []g = a.get("GUID");
                if(g != null)
                {
                    UUID PeerGUID = Util.Base64ToGUID(new String(g, StandardCharsets.US_ASCII));
                    if(!PeerGUID.equals(s_MyGUID))
                    {
                        String Name = null;
                        final byte[] nv = g = a.get("Name");
                        if(nv != null)
                            Name = Util.safely(() -> new String(nv, StandardCharsets.UTF_8));
                        if(Name == null)
                            Name = si.getServiceName();
                        //Name = Name.replace("\\\\009", "\t").replace("\\\\032", " ");
                        final Peer se = new Peer(Name, si.getHost(), si.getPort(), PeerGUID);
                        runOnUiThread(() -> displayFoundService(se));
                    }
                }

                resolveNext();
            }

            public void onResolveFailed(NsdServiceInfo si, int errorCode)
            {
                android.util.Log.d("Discover", "Resolve failed for " + si.getServiceName());
                resolveNext();
            }
        });
    }

    @Override
    public void onServiceLost(NsdServiceInfo si)
    {
        int i, n = m_Ada.getCount();
        for(i=0;i<n;i++)
        {
            Peer se = m_Ada.getItem(i);
            if(se.equals(si)) //Compare address/port
            {
                m_Ada.remove(se);
                if(i == m_SelIndex)
                {
                    findViewById(R.id.Send).setEnabled(false);
                    ((TextView)findViewById(R.id.SendingTo)).setText(null);
                    findViewById(R.id.TheText).setEnabled(false);
                    m_SelIndex = -1;
                }
                break;
            }
        }
    }

    @Override
    public void onStartDiscoveryFailed(String serviceType, int errorCode) {}

    @Override
    public void onStopDiscoveryFailed(String serviceType, int errorCode){}
    //endregion

    //region Receiving messages

    private void startReceiving()
    {
        new Thread(s_ReceiveThreadProc).start();
    }

    private Runnable s_ReceiveThreadProc = new Runnable()
    {
        public void run()
        {
            Thread.currentThread().setName("Receiver");
            while(m_Channel != null && m_Channel.isOpen())
            {
                try
                {
                    m_Sel.select();

                    //Receive if woken by incoming data
                    InetSocketAddress from = (InetSocketAddress)m_Channel.receive(s_ReceiveGram);
                    if(from != null)
                    {
                        byte []data = Arrays.copyOf(s_ReceiveGram.array(), s_ReceiveGram.position());
                        s_ReceiveGram.rewind();
                        runOnUiThread(() -> onReceived(data, from.getAddress(), from.getPort()));
                    }

                    //Send if woken manually
                    OutgoingMessage msg;
                    while((msg = m_SendQueue.poll()) != null)
                    {
                        try
                        {
                            m_Channel.send(msg.data, msg.to);
                        }
                        catch(IOException exc)
                        {
                            android.util.Log.d("Receive", "Send exc", exc);
                        }
                    }
                }
                catch(IOException exc)
                {
                    android.util.Log.d("Receive", "Select/receive exc", exc);
                }
            }
        }
    };

    //UI thread callback on new message
    private void onReceived(byte [] data, InetAddress fromAddr, int fromPort)
    {
        if(data.length >= V0_HEADER_SIZE)
        {
            int Version = (int)data[0] & 0xFF |
                ((int)data[1] & 0xFF) << 8 |
                ((int)data[2] & 0xFF) << 16 |
                ((int)data[3] & 0xFF) << 24;
            if(Version == 0)
            {
                UUID peerGUID = Util.bytesToGUID(data, 4);
                int i, n = m_Ada.getCount();
                String from = null;
                for(i=0;i<n;i++)
                {
                    Peer se = m_Ada.getItem(i);
                    if(se.m_GUID.equals(peerGUID))
                    {
                        from = se.name();
                        se.cameFrom(fromAddr, fromPort);
                        break;
                    }
                }

                if(i == n) //Not found in the peer list...
                {
                    from = Util.prefs(this).getString("NameForGUID_" + peerGUID.toString(), null);
                    if(from == null)
                        from = fromAddr.toString() + ":" + Integer.toString(fromPort);
                    m_Ada.add(new Peer(from, fromAddr, fromPort, peerGUID));
                }

                String s = new String(data, V0_HEADER_SIZE, data.length - V0_HEADER_SIZE, StandardCharsets.UTF_8);
                if(m_MsgDlg != null)
                    m_MsgDlg.queueMessage(from, s);
                else
                {
                    m_MsgDlg = new MessageDlg(this, from, s);
                    m_MsgDlg.setOnDismissListener(this);
                    m_MsgDlg.show();
                }
            }
        }
    }
    //endregion

    static class RegListener implements NsdManager.RegistrationListener
    {
        public void onUnregistrationFailed(NsdServiceInfo serviceInfo, int errorCode) {}
        public void onServiceUnregistered(NsdServiceInfo serviceInfo) {}
        public void onServiceRegistered(NsdServiceInfo serviceInfo) {}
        public void onRegistrationFailed(NsdServiceInfo serviceInfo, int errorCode) { }
    }
}
