using System;
using System.Collections.Generic;
using System.Drawing;
using System.Text;
using System.Windows.Forms;
using WheelForge.Core;
using WheelForge.Hid;

namespace WheelForge.Ui
{
    public class MainForm : Form
    {
        // ---- device plumbing ------------------------------------------------
        private readonly HidReader _reader = new HidReader();
        private List<HidDeviceInfo> _devices = new List<HidDeviceInfo>();
        private List<SerialBoard> _serialBoards = new List<SerialBoard>();
        private bool _showAllHid;

        // The reader thread can deliver a thousand reports a second. It parks the
        // newest one here and the UI timer picks it up at screen rate; marshalling
        // every report to the UI thread would drown it.
        private volatile HidState _latest;

        // Whichever device is currently feeding the app -- a real HID device
        // through _reader, or a bridge board over serial. The Monitor and
        // Calibration pages read this, so both work the same either way.
        private HidReportLayout _activeLayout;
        private WheelLink _bridgeLink;
        private string _bridgePort;

        private bool InputActive
        {
            get { return _reader.IsRunning || _bridgeLink != null; }
        }

        // ---- chrome ---------------------------------------------------------
        private ComboBox _deviceBox;
        private Button _rescanButton;
        private Button _connectButton;
        private CheckBox _showAllBox;
        private Label _statusLabel;
        private Panel _contentHost;
        private readonly List<NavButton> _navButtons = new List<NavButton>();
        private readonly Dictionary<string, Control> _pages = new Dictionary<string, Control>();
        private readonly Timer _uiTimer = new Timer();

        // ---- monitor page ---------------------------------------------------
        private SteeringDial _dial;
        private ComboBox _rangeBox;
        private Panel _axisHost;
        private readonly List<AxisBar> _axisBars = new List<AxisBar>();
        private int _steeringAxisIndex = -1;
        private ButtonGrid _buttonGrid;
        private Label _rawLabel;
        private Label _rateLabel;

        // ---- calibration page -----------------------------------------------
        private CalibrationPage _calibrationPage;

        // ---- wheel test page ------------------------------------------------
        private WheelTestPage _wheelTestPage;

        public MainForm() : this("monitor", false)
        {
        }

        public MainForm(string startPage, bool autoConnect)
        {
            Text = "WheelForge";
            Size = new Size(1060, 720);
            MinimumSize = new Size(900, 620);
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Theme.Window;
            ForeColor = Theme.Text;
            Font = Theme.UiFont;

            // Reuse the icon already embedded in the executable rather than
            // shipping a second copy as a resource.
            try
            {
                Icon = System.Drawing.Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            }
            catch (Exception)
            {
                // Running from somewhere the icon cannot be read. Cosmetic only.
            }

            BuildChrome();
            BuildPages();
            SelectPage(_pages.ContainsKey(startPage) ? startPage : "monitor");

            _reader.StateChanged += OnReaderState;
            _reader.Stopped += OnReaderStopped;

            _uiTimer.Interval = 16;          // ~60 Hz
            _uiTimer.Tick += OnUiTick;
            _uiTimer.Start();

            Load += delegate
            {
                Rescan();
                if (autoConnect && SelectedDevice() != null) ToggleConnection();
            };
            FormClosing += delegate
            {
                _uiTimer.Stop();
                _reader.Close();
                if (_wheelTestPage != null) _wheelTestPage.ShutDown();
            };
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            Theme.ApplyDarkTitleBar(Handle);
        }

        // =====================================================================
        //  Chrome
        // =====================================================================

        private void BuildChrome()
        {
            Panel top = new Panel();
            top.Dock = DockStyle.Top;
            top.Height = 62;
            top.BackColor = Theme.Rail;
            top.Paint += delegate(object s, PaintEventArgs e)
            {
                using (Pen p = new Pen(Theme.Line))
                    e.Graphics.DrawLine(p, 0, top.Height - 1, top.Width, top.Height - 1);
            };

            Label brand = Theme.MakeLabel("WheelForge", Theme.TitleFont, Theme.Text, 18, 9);
            top.Controls.Add(brand);

            Label tag = Theme.MakeLabel("wheel configurator", Theme.UiFontSmall, Theme.TextFaint, 20, 36);
            top.Controls.Add(tag);

            _deviceBox = new ComboBox();
            _deviceBox.DropDownStyle = ComboBoxStyle.DropDownList;
            _deviceBox.SelectedIndexChanged += delegate { UpdateConnectButton(); };
            top.Controls.Add(_deviceBox);

            _rescanButton = new Button();
            _rescanButton.Text = "Rescan";
            _rescanButton.Size = new Size(80, 26);
            _rescanButton.Click += delegate { Rescan(); };
            top.Controls.Add(_rescanButton);

            _connectButton = new Button();
            _connectButton.Text = "Connect";
            _connectButton.Size = new Size(104, 26);
            _connectButton.Click += delegate { ToggleConnection(); };
            top.Controls.Add(_connectButton);

            _showAllBox = new CheckBox();
            _showAllBox.Text = "All HID devices";
            _showAllBox.AutoSize = true;
            _showAllBox.CheckedChanged += delegate
            {
                _showAllHid = _showAllBox.Checked;
                PopulateDeviceBox();
            };
            top.Controls.Add(_showAllBox);

            _statusLabel = Theme.MakeLabel("Scanning...", Theme.UiFontSmall, Theme.TextDim, 192, 40);
            top.Controls.Add(_statusLabel);

            // Anchors on a panel that is docked after its children are added
            // compute their margins from the pre-dock width, which throws the
            // right hand controls off the edge. Positioning from the real width
            // on every resize is predictable.
            top.Resize += delegate { LayoutTopBar(top); };
            LayoutTopBar(top);

            // Left rail
            Panel rail = new Panel();
            rail.Dock = DockStyle.Left;
            rail.Width = 168;
            rail.BackColor = Theme.Rail;

            _contentHost = new Panel();
            _contentHost.Dock = DockStyle.Fill;
            _contentHost.BackColor = Theme.Window;
            _contentHost.Padding = new Padding(14, 12, 14, 12);

            // Docking is applied from the last added control backwards, so the
            // one added last claims its edge first and the Fill added first gets
            // whatever is left. Adding these in the obvious order instead --
            // or reaching for BringToFront -- puts the Fill panel underneath the
            // top bar, which silently hides the first row of every page.
            Controls.Add(_contentHost);
            Controls.Add(rail);
            Controls.Add(top);

            string[][] nav = new string[][]
            {
                new string[] { "monitor",     "Monitor" },
                new string[] { "calibration", "Calibration" },
                new string[] { "wheeltest",   "Wheel test" },
                new string[] { "firmware",    "Firmware" },
                new string[] { "setup",       "Setup guide" },
                new string[] { "boards",      "Boards" },
                new string[] { "modes",       "Output mode" },
                new string[] { "about",       "About" }
            };

            int y = 14;
            foreach (string[] entry in nav)
            {
                NavButton nb = new NavButton(entry[1]);
                nb.Tag = entry[0];
                nb.Location = new Point(0, y);
                nb.Width = rail.Width;
                nb.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
                nb.Click += delegate(object s, EventArgs e) { SelectPage((string)((NavButton)s).Tag); };
                rail.Controls.Add(nb);
                _navButtons.Add(nb);
                y += 42;
            }

            Label version = Theme.MakeLabel("v0.3", Theme.UiFontSmall, Theme.TextFaint, 0, 0);
            version.AutoSize = false;
            version.Height = 30;
            version.Padding = new Padding(18, 8, 0, 0);
            version.Dock = DockStyle.Bottom;
            rail.Controls.Add(version);

            Theme.StyleButton(_rescanButton);
            Theme.StylePrimary(_connectButton);
            _showAllBox.ForeColor = Theme.TextDim;
            _deviceBox.BackColor = Theme.PanelHi;
            _deviceBox.ForeColor = Theme.Text;
            _deviceBox.FlatStyle = FlatStyle.Flat;
        }

        private void LayoutTopBar(Panel top)
        {
            int w = top.ClientSize.Width;
            if (w < 400) return;

            const int edge = 16;
            const int gap = 8;

            _showAllBox.Location = new Point(w - edge - _showAllBox.PreferredSize.Width, 21);
            _connectButton.Location = new Point(_showAllBox.Left - gap - _connectButton.Width, 17);
            _rescanButton.Location = new Point(_connectButton.Left - gap - _rescanButton.Width, 17);

            int boxLeft = 192;
            int boxWidth = _rescanButton.Left - gap - boxLeft;
            _deviceBox.Location = new Point(boxLeft, 18);
            _deviceBox.Width = boxWidth < 120 ? 120 : boxWidth;
        }

        private void BuildPages()
        {
            _calibrationPage = new CalibrationPage();

            _pages["monitor"] = BuildMonitorPage();
            _pages["calibration"] = _calibrationPage;
            _wheelTestPage = new WheelTestPage();
            _wheelTestPage.BridgeConnected += OnBridgeConnected;
            _wheelTestPage.BridgeDisconnected += OnBridgeDisconnected;
            _pages["wheeltest"] = _wheelTestPage;

            FirmwarePage firmware = new FirmwarePage();
            firmware.ReleasePort = delegate(string port)
            {
                if (_wheelTestPage != null) _wheelTestPage.ReleaseIfUsing(port);
            };
            _pages["firmware"] = firmware;
            _pages["setup"] = new SetupGuidePage();
            _pages["boards"] = InfoPages.BuildBoardsPage();
            _pages["modes"] = InfoPages.BuildModesPage();
            _pages["about"] = InfoPages.BuildAboutPage();

            foreach (KeyValuePair<string, Control> kv in _pages)
            {
                kv.Value.Dock = DockStyle.Fill;
                kv.Value.Visible = false;
                _contentHost.Controls.Add(kv.Value);
            }
        }

        private void SelectPage(string id)
        {
            foreach (KeyValuePair<string, Control> kv in _pages)
                kv.Value.Visible = (kv.Key == id);

            foreach (NavButton nb in _navButtons)
                nb.Selected = ((string)nb.Tag == id);
        }

        // =====================================================================
        //  Monitor page
        // =====================================================================

        private Control BuildMonitorPage()
        {
            TableLayoutPanel grid = new TableLayoutPanel();
            grid.BackColor = Theme.Window;
            grid.ColumnCount = 2;
            grid.RowCount = 3;
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 236));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 266));
            grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 84));

            // --- steering dial
            Card dialCard = new Card("Steering");
            dialCard.Dock = DockStyle.Fill;
            dialCard.Margin = new Padding(0, 0, 10, 10);

            _dial = new SteeringDial();
            _dial.Dock = DockStyle.Fill;
            _dial.BackColor = Theme.Panel;
            dialCard.Controls.Add(_dial);

            Panel rangeRow = new Panel();
            rangeRow.Dock = DockStyle.Bottom;
            rangeRow.Height = 30;
            rangeRow.BackColor = Theme.Panel;

            Label rangeLabel = Theme.MakeLabel("Range", Theme.UiFontSmall, Theme.TextDim, 2, 8);
            rangeRow.Controls.Add(rangeLabel);

            _rangeBox = new ComboBox();
            _rangeBox.DropDownStyle = ComboBoxStyle.DropDownList;
            _rangeBox.Items.AddRange(new object[] { "180°", "270°", "360°", "540°", "720°", "900°", "1080°" });
            _rangeBox.SelectedIndex = 5;
            _rangeBox.Location = new Point(52, 4);
            _rangeBox.Width = 92;
            _rangeBox.BackColor = Theme.PanelHi;
            _rangeBox.ForeColor = Theme.Text;
            _rangeBox.FlatStyle = FlatStyle.Flat;
            _rangeBox.SelectedIndexChanged += delegate
            {
                string s = (string)_rangeBox.SelectedItem;
                _dial.RangeDegrees = int.Parse(s.Replace("°", ""));
            };
            rangeRow.Controls.Add(_rangeBox);

            dialCard.Controls.Add(rangeRow);
            rangeRow.BringToFront();
            grid.Controls.Add(dialCard, 0, 0);

            // --- axes
            Card axesCard = new Card("Axes");
            axesCard.Dock = DockStyle.Fill;
            axesCard.Margin = new Padding(0, 0, 0, 10);

            _axisHost = new Panel();
            _axisHost.Dock = DockStyle.Fill;
            _axisHost.BackColor = Theme.Panel;
            _axisHost.AutoScroll = true;
            axesCard.Controls.Add(_axisHost);
            grid.Controls.Add(axesCard, 1, 0);

            // --- buttons
            Card buttonCard = new Card("Buttons");
            buttonCard.Dock = DockStyle.Fill;
            buttonCard.Margin = new Padding(0, 0, 0, 10);

            Panel buttonHost = new Panel();
            buttonHost.Dock = DockStyle.Fill;
            buttonHost.BackColor = Theme.Panel;
            buttonHost.AutoScroll = true;

            _buttonGrid = new ButtonGrid();
            _buttonGrid.Dock = DockStyle.Top;
            _buttonGrid.BackColor = Theme.Panel;
            buttonHost.Controls.Add(_buttonGrid);
            buttonCard.Controls.Add(buttonHost);

            Label hint = Theme.MakeLabel(
                "Cells stay outlined once seen, so you can find which switch is which.",
                Theme.UiFontSmall, Theme.TextFaint, 0, 0);
            hint.Dock = DockStyle.Bottom;
            hint.AutoSize = false;
            hint.Height = 18;
            buttonCard.Controls.Add(hint);
            hint.BringToFront();

            grid.Controls.Add(buttonCard, 0, 1);
            grid.SetColumnSpan(buttonCard, 2);

            // --- raw report
            Card rawCard = new Card(null);
            rawCard.Dock = DockStyle.Fill;
            rawCard.Margin = new Padding(0, 0, 0, 0);
            rawCard.Padding = new Padding(13, 10, 13, 10);

            _rateLabel = Theme.MakeLabel("not connected", Theme.UiFontSmall, Theme.TextDim, 0, 0);
            _rateLabel.Dock = DockStyle.Top;
            _rateLabel.AutoSize = false;
            _rateLabel.Height = 17;
            rawCard.Controls.Add(_rateLabel);

            _rawLabel = Theme.MakeLabel("", Theme.MonoFontSmall, Theme.TextFaint, 0, 0);
            _rawLabel.Dock = DockStyle.Fill;
            _rawLabel.AutoSize = false;
            rawCard.Controls.Add(_rawLabel);
            _rawLabel.BringToFront();

            grid.Controls.Add(rawCard, 0, 2);
            grid.SetColumnSpan(rawCard, 2);

            return grid;
        }

        // Rebuilds the axis strip to match whatever the connected device declares.
        private void RebuildAxes()
        {
            _axisHost.SuspendLayout();
            foreach (AxisBar bar in _axisBars) _axisHost.Controls.Remove(bar);
            _axisBars.Clear();
            _steeringAxisIndex = -1;

            HidReportLayout layout = _activeLayout;
            if (layout == null || layout.Axes.Count == 0)
            {
                _axisHost.ResumeLayout();
                _axisHost.Invalidate();
                return;
            }

            // The steering axis is whichever comes first: an explicit Steering
            // usage, or plain X. Steering is checked on either usage page --
            // see the note in UsageNames.
            for (int i = 0; i < layout.Axes.Count; i++)
            {
                HidAxis a = layout.Axes[i];
                if (a.Usage == 0xC8 && UsageNames.IsSimulationUsage(a.UsagePage, a.Usage))
                {
                    _steeringAxisIndex = i;
                    break;
                }
            }
            if (_steeringAxisIndex < 0)
            {
                for (int i = 0; i < layout.Axes.Count; i++)
                {
                    HidAxis a = layout.Axes[i];
                    if (a.UsagePage == 0x01 && a.Usage == 0x30) { _steeringAxisIndex = i; break; }
                }
            }

            int y = 6;
            for (int i = 0; i < layout.Axes.Count; i++)
            {
                HidAxis a = layout.Axes[i];

                AxisBar bar = new AxisBar();
                bar.Caption = a.Name;
                bar.BackColor = Theme.Panel;
                bar.Location = new Point(2, y);
                bar.Width = _axisHost.ClientSize.Width - 20;
                bar.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
                bar.Bipolar = (i == _steeringAxisIndex);
                bar.BarColour = ColourForAxis(a, i == _steeringAxisIndex);
                bar.RawText = "--";

                _axisHost.Controls.Add(bar);
                _axisBars.Add(bar);
                y += 28;
            }

            _axisHost.ResumeLayout();
        }

        private static Color ColourForAxis(HidAxis a, bool isSteering)
        {
            if (isSteering) return Theme.Accent;

            if (UsageNames.IsSimulationUsage(a.UsagePage, a.Usage))
            {
                if (a.Usage == 0xC4 || a.Usage == 0xBB) return Theme.Good;   // accelerator
                if (a.Usage == 0xC5) return Theme.Bad;                        // brake
                if (a.Usage == 0xC6) return Theme.Cool;                       // clutch
            }

            if (a.IsHat) return Theme.TextDim;
            return Theme.Cool;
        }

        // =====================================================================
        //  Device handling
        // =====================================================================

        private void Rescan()
        {
            Cursor = Cursors.WaitCursor;
            try
            {
                _devices = HidEnumerator.Enumerate();
            }
            catch (Exception ex)
            {
                _devices = new List<HidDeviceInfo>();
                SetStatus("Scan failed: " + ex.Message, Theme.Bad);
            }

            try
            {
                _serialBoards = SerialBoardScanner.Scan();
            }
            catch (Exception)
            {
                _serialBoards = new List<SerialBoard>();
            }
            finally
            {
                Cursor = Cursors.Default;
            }

            PopulateDeviceBox();
        }

        private void PopulateDeviceBox()
        {
            List<HidDeviceInfo> shown = new List<HidDeviceInfo>();
            foreach (HidDeviceInfo d in _devices)
                if (_showAllHid || d.IsGameController) shown.Add(d);

            _deviceBox.Items.Clear();
            foreach (HidDeviceInfo d in shown)
            {
                string known = d.KnownAs;
                string label = d.DisplayName;
                if (known != null) label = known + " — " + label;
                if (d.AxisCount > 0 || d.ButtonCount > 0)
                    label += "  (" + d.AxisCount + " axes, " + d.ButtonCount + " buttons)";
                _deviceBox.Items.Add(label);
            }

            // Boards on a COM port go in the same list. They are not game
            // controllers and never will be, but this is where someone looks
            // for the Arduino they just plugged in, so this is where it has to
            // appear -- with what to do about it.
            foreach (SerialBoard b in _serialBoards)
            {
                // Say what is true right now rather than repeating an
                // instruction the user has already followed.
                bool live = _bridgePort != null
                    && string.Equals(_bridgePort, b.PortName, StringComparison.OrdinalIgnoreCase);
                _deviceBox.Items.Add(b.DisplayName + (live
                    ? "  —  connected, streaming"
                    : "  —  serial board, press Set up"));
            }

            _deviceBox.Tag = shown;

            if (shown.Count == 0 && _serialBoards.Count == 0)
            {
                SetStatus(_showAllHid
                    ? "No HID devices found at all — that is unusual."
                    : "No controllers or boards found. Plug one in, or tick 'All HID devices'.",
                    Theme.Warn);
            }
            else
            {
                int pick = 0;
                HidDeviceInfo best = HidEnumerator.PickBestWheel(shown);
                if (best != null) pick = shown.IndexOf(best);
                else if (shown.Count == 0) pick = 0;
                _deviceBox.SelectedIndex = pick;

                if (shown.Count == 0)
                {
                    SerialBoard b = _serialBoards[0];
                    SetStatus(b.DisplayName + " is plugged in. It is a serial board, so it will "
                            + "not appear as a controller here " + "\u2014" + " press Set up.", Theme.Warn);
                }
                else
                {
                    string text = shown.Count + (shown.Count == 1 ? " controller" : " controllers");
                    if (_serialBoards.Count > 0)
                        text += ", plus " + _serialBoards.Count + " serial board"
                              + (_serialBoards.Count == 1 ? "" : "s");
                    SetStatus(text + " found", Theme.TextDim);
                }
            }

            UpdateConnectButton();
        }

        // The serial board sitting at this position in the combo, or null when
        // a real HID device is selected.
        private SerialBoard SelectedSerialBoard()
        {
            List<HidDeviceInfo> shown = _deviceBox.Tag as List<HidDeviceInfo>;
            int hidCount = shown == null ? 0 : shown.Count;
            int i = _deviceBox.SelectedIndex - hidCount;
            if (i < 0 || i >= _serialBoards.Count) return null;
            return _serialBoards[i];
        }

        private HidDeviceInfo SelectedDevice()
        {
            List<HidDeviceInfo> shown = _deviceBox.Tag as List<HidDeviceInfo>;
            if (shown == null) return null;
            int i = _deviceBox.SelectedIndex;
            if (i < 0 || i >= shown.Count) return null;
            return shown[i];
        }

        private void UpdateConnectButton()
        {
            bool connected = _reader.IsRunning;
            SerialBoard serial = connected ? null : SelectedSerialBoard();

            if (connected) _connectButton.Text = "Disconnect";
            else if (serial != null) _connectButton.Text = "Set up";
            else _connectButton.Text = "Connect";

            _connectButton.Enabled = connected || SelectedDevice() != null || serial != null;
            _deviceBox.Enabled = !connected;
            _rescanButton.Enabled = !connected;
            _showAllBox.Enabled = !connected;
        }

        private void ToggleConnection()
        {
            if (_reader.IsRunning)
            {
                _reader.Close();
                _latest = null;
                _activeLayout = null;
                RebuildAxes();
                _buttonGrid.EmptyText = "No device connected.";
                _buttonGrid.ButtonCount = 0;
                _calibrationPage.ClearLive();
                _calibrationPage.SetDevice(null, null);
                _dial.Value = 0.5;
                _rawLabel.Text = "";
                _rateLabel.Text = "not connected";
                SetStatus("Disconnected", Theme.TextDim);
                UpdateConnectButton();
                return;
            }

            // A board on a COM port cannot be opened as a controller. Send the
            // user to the page that can actually do something with it, with the
            // board and port already filled in.
            SerialBoard serial = SelectedSerialBoard();
            if (serial != null)
            {
                RouteSerialBoard(serial);
                return;
            }

            HidDeviceInfo device = SelectedDevice();
            if (device == null) return;

            string error = _reader.Open(device);
            if (error != null)
            {
                SetStatus(error, Theme.Bad);
                MessageBox.Show(this, error, "Could not open device",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            _activeLayout = _reader.Layout;
            RebuildAxes();
            _buttonGrid.EmptyText = "This device declares no buttons.";
            _buttonGrid.ButtonCount = _activeLayout.ButtonCount;
            _calibrationPage.SetDevice(device, _activeLayout);
            _reader.Start();

            string known = device.KnownAs;
            SetStatus("Connected to " + (known != null ? known : device.DisplayName)
                      + "  ·  reading shared, a game can keep the wheel", Theme.Good);
            UpdateConnectButton();
        }

        private void RouteSerialBoard(SerialBoard board)
        {
            BoardProfile profile = board.BoardId != null ? BoardCatalog.ById(board.BoardId) : null;

            const string Break = "\r\n\r\n";

            // Ask the board what it is rather than assuming. This is the only
            // way to tell an already-flashed wheel from a blank one, and
            // guessing sends people to the wrong page.
            Cursor = Cursors.WaitCursor;
            SetStatus("Asking " + board.PortName + " what it is...", Theme.TextDim);
            Refresh();

            Dictionary<string, string> settings = null;
            try
            {
                settings = WheelLink.Probe(board.PortName, 4000);
            }
            catch (Exception)
            {
                settings = null;
            }
            finally
            {
                Cursor = Cursors.Default;
            }

            string what = profile != null ? profile.Name : board.DisplayName;

            if (settings != null)
            {
                // Already flashed. Send them where they can actually use it.
                string version = settings.ContainsKey("version") ? settings["version"] : "?";
                string transport = settings.ContainsKey("transport") ? settings["transport"] : "";

                SetStatus(what + " on " + board.PortName + " is running WheelForge "
                          + version + ".", Theme.Good);

                string message =
                    what + " on " + board.PortName + " is already running WheelForge firmware "
                  + version + "." + Break;

                if (transport == "bridge")
                {
                    message +=
                        "It is a bridge board, so it never appears in the device list above. "
                      + "To use it: connect to " + board.PortName + " on the Wheel test page and "
                      + "tick Feed vJoy. It then shows up to games as a vJoy controller." + Break
                      + "Open the Wheel test page now?";
                }
                else
                {
                    message +=
                        "It reports itself as a USB controller, so it should also be in the list "
                      + "above. Use the Wheel test page to set the encoder and try the motor."
                      + Break + "Open the Wheel test page now?";
                }

                if (MessageBox.Show(this, message, "Board is set up",
                        MessageBoxButtons.YesNo, MessageBoxIcon.Information) == DialogResult.Yes)
                {
                    WheelTestPage test = _pages["wheeltest"] as WheelTestPage;
                    if (test != null) test.Preselect(board.PortName);
                    SelectPage("wheeltest");
                }
                return;
            }

            // Nothing answered: either it is not flashed, or the port is busy.
            SetStatus(what + " on " + board.PortName + " did not answer. It probably needs "
                      + "flashing.", Theme.Warn);

            string prompt =
                what + " is on " + board.PortName + ", but it did not answer." + Break
              + "That usually means it is not running WheelForge firmware yet. It can also mean "
              + "the port is open in another program." + Break;

            if (profile != null)
                prompt += "Flash it on the Firmware page, then come back here." + Break;
            else
                prompt += "WheelForge could not identify this board from its USB id ("
                        + board.VidPid + "), so pick it by hand on the Firmware page." + Break;

            prompt += "Open the Firmware page now?";

            if (MessageBox.Show(this, prompt, "Board needs flashing",
                    MessageBoxButtons.YesNo, MessageBoxIcon.Information) != DialogResult.Yes)
                return;

            FirmwarePage page = _pages["firmware"] as FirmwarePage;
            if (page != null) page.Preselect(board.BoardId, board.PortName);
            SelectPage("firmware");
        }

        // A bridge board has no HID descriptor for Windows to parse, so it is
        // given the hand written layout that matches what the firmware sends.
        // From here on the Monitor and Calibration pages cannot tell the
        // difference, which is the point.
        private void OnBridgeConnected(WheelLink link)
        {
            if (_reader.IsRunning) _reader.Close();

            _bridgeLink = link;
            _bridgePort = link.PortName;
            _activeLayout = BridgeLayout.Build();
            _latest = null;

            link.Input += OnBridgeInput;

            RebuildAxes();
            _buttonGrid.EmptyText = "No buttons pressed yet.";
            _buttonGrid.ButtonCount = _activeLayout.ButtonCount;
            _calibrationPage.SetDevice(null, _activeLayout);

            SetStatus("Bridge board on " + _bridgePort
                      + " — monitoring and calibration now work on it.", Theme.Good);
            PopulateDeviceBox();
        }

        private void OnBridgeDisconnected()
        {
            if (_bridgeLink != null) _bridgeLink.Input -= OnBridgeInput;

            _bridgeLink = null;
            _bridgePort = null;
            _activeLayout = null;
            _latest = null;

            RebuildAxes();
            _buttonGrid.EmptyText = "No device connected.";
            _buttonGrid.ButtonCount = 0;
            _calibrationPage.ClearLive();
            _calibrationPage.SetDevice(null, null);
            _dial.Value = 0.5;
            _rawLabel.Text = "";
            _rateLabel.Text = "not connected";

            SetStatus("Bridge board disconnected.", Theme.TextDim);
            PopulateDeviceBox();
        }

        // Arrives on the serial reader thread. Converted into the same shape a
        // HID report decodes to, and parked for the UI timer like any other.
        private void OnBridgeInput(WheelInput input)
        {
            HidReportLayout layout = _activeLayout;
            if (layout == null || input == null) return;

            HidState state = new HidState();
            state.AxisRaw = new int[4];
            state.AxisNormalised = new double[4];

            state.AxisRaw[0] = input.Steering;
            state.AxisNormalised[0] = (input.Steering + 32768.0) / 65535.0;

            SetPedal(state, 1, input.Throttle);
            SetPedal(state, 2, input.Brake);
            SetPedal(state, 3, input.Clutch);

            state.PressedButtons = input.PressedButtons();
            _latest = state;
        }

        private static void SetPedal(HidState state, int index, ushort value)
        {
            double n = value / 16383.0;
            if (n > 1.0) n = 1.0;
            state.AxisRaw[index] = value;
            state.AxisNormalised[index] = n;
        }

        private void SetStatus(string text, Color colour)
        {
            _statusLabel.Text = text;
            _statusLabel.ForeColor = colour;
        }

        // Reader thread. Park the state and return immediately.
        private void OnReaderState(HidState state)
        {
            _latest = state;
        }

        private void OnReaderStopped(string reason)
        {
            if (reason == null) return;
            if (IsDisposed || !IsHandleCreated) return;

            try
            {
                BeginInvoke((MethodInvoker)delegate
                {
                    SetStatus(reason, Theme.Bad);
                    _reader.Close();
                    UpdateConnectButton();
                });
            }
            catch (InvalidOperationException)
            {
                // Window went away while the reader was shutting down.
            }
        }

        // UI thread, ~60 Hz.
        private void OnUiTick(object sender, EventArgs e)
        {
            if (!InputActive) return;

            if (_bridgeLink != null)
            {
                _rateLabel.Text = "bridge board on " + _bridgePort
                                + "   ·   streaming over serial";
            }
            else
            {
                _rateLabel.Text = "input reports: " + _reader.ReportsPerSecond.ToString("0")
                                + " /s   ·   report length "
                                + (_reader.Device != null ? _reader.Device.InputReportLength : 0) + " bytes";
            }

            HidState state = _latest;
            if (state == null)
            {
                // Connected, but the device has not sent anything yet. vJoy does
                // this until a feeder moves an axis; a real wheel reports
                // continuously.
                _rawLabel.Text = "waiting for the first input report...";
                return;
            }

            for (int i = 0; i < _axisBars.Count && i < state.AxisNormalised.Length; i++)
            {
                _axisBars[i].Value = state.AxisNormalised[i];
                _axisBars[i].RawText = state.AxisRaw[i].ToString();
            }

            if (_steeringAxisIndex >= 0 && _steeringAxisIndex < state.AxisNormalised.Length)
                _dial.Value = state.AxisNormalised[_steeringAxisIndex];

            _buttonGrid.SetPressed(state.PressedButtons);
            _calibrationPage.PushState(state);

            if (state.Raw != null) _rawLabel.Text = Hex(state.Raw);
            else if (_bridgeLink != null) _rawLabel.Text = "";
        }

        private static string Hex(byte[] data)
        {
            StringBuilder sb = new StringBuilder(data.Length * 3);
            for (int i = 0; i < data.Length; i++)
            {
                if (i > 0) sb.Append(i % 8 == 0 ? "  " : " ");
                sb.Append(data[i].ToString("X2"));
            }
            return sb.ToString();
        }
    }
}

