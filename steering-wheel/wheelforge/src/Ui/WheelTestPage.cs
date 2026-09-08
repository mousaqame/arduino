using System;
using System.Drawing;
using System.Collections.Generic;
using System.Globalization;
using System.Windows.Forms;
using WheelForge.Core;

namespace WheelForge.Ui
{
    // Settings and the motor test, both over the board's serial link.
    //
    // This is the only page that can make the wheel move, so it is deliberately
    // awkward to do by accident: the motor output has to be armed with its own
    // checkbox, the torque ceiling starts low, and Stop is always reachable.
    //
    // The board drops any effect it has not heard about for three seconds. That
    // watchdog, not this UI, is what actually keeps things safe -- if the app
    // crashes or the cable is pulled, the motor releases on its own.
    public class WheelTestPage : Panel
    {
        private readonly WheelLink _link = new WheelLink();
        private readonly VJoyFeeder _vjoy = new VJoyFeeder();

        private CheckBox _feedVJoy;
        private Label _vjoyStatus;

        private ComboBox _portBox;
        private Button _refreshButton;
        private Button _connectButton;
        private Label _status;

        private NumericUpDown _ppr;
        private NumericUpDown _rotation;
        private CheckBox _invert;
        private ComboBox _driver;
        private Slider _maxTorque;
        private Label _maxTorqueLabel;
        private Slider _ffbGain;
        private Label _ffbGainLabel;
        private CheckBox _ffbInvert;

        private ComboBox _encType;
        private NumericUpDown _encA, _encB;
        private NumericUpDown _motorA, _motorB, _motorDir, _motorEn;
        private NumericUpDown _pedalA, _pedalB, _pedalC;
        private Panel _settingsBody;
        private Button _findPins;
        private Button _measurePpr;
        private long _pprStartCounts;
        private bool _pprMeasuring;

        // Pin finder state. Watching every pin while the wheel is turned is the
        // only way to answer "my encoder does nothing" without guessing.
        private readonly Timer _scanTimer = new Timer();
        private DateTime _scanEnds;
        private uint _scanLastMask;
        private bool _scanHaveMask;
        private uint _scanChanged;
        private int _scanPinCount;
        private int _scanSamples;
        private Button _readButton;
        private Button _applyButton;
        private Button _saveButton;
        private Button _centreButton;

        private CheckBox _armMotor;
        private Slider _strength;
        private Label _strengthLabel;
        private Button _stopButton;
        private Button[] _effectButtons = new Button[0];
        private Label _effectStatus;
        private Label _ffbLabel;

        private SteeringDial _dial;
        private Label _liveLabel;
        private TextBox _log;

        private readonly Timer _poll = new Timer();

        // Opening the port resets an Uno, so it spends the first couple of
        // seconds in its bootloader and never hears a question asked once.
        // This keeps asking until the board answers.
        private readonly Timer _configTimer = new Timer();
        private int _configTries;
        private bool _configReceived;

        // Raised when a board on a COM port starts or stops streaming, so the
        // Monitor and Calibration pages can use it like any other device. A
        // bridge board that only ever fed this page was the reason calibration
        // did nothing on an Uno.
        public event Action<WheelLink> BridgeConnected;
        public event Action BridgeDisconnected;
        private bool _suppress;

        public WheelTestPage()
        {
            BackColor = Theme.Window;
            Build();

            _link.Line += OnLine;
            _link.Closed += OnLinkClosed;
            _link.Input += OnBridgeInput;

            _poll.Interval = 200;
            _poll.Tick += delegate { if (_link.IsOpen) _link.Send("STATUS"); };

            _scanTimer.Interval = 40;
            _scanTimer.Tick += OnScanTick;

            _configTimer.Interval = 600;
            _configTimer.Tick += OnConfigTick;

            RefreshPorts();
            UpdateEnabled();
        }

        // =====================================================================

        private void Build()
        {
            TableLayoutPanel grid = new TableLayoutPanel();
            grid.Dock = DockStyle.Fill;
            grid.BackColor = Theme.Window;
            grid.ColumnCount = 2;
            grid.RowCount = 3;
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50f));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50f));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 84));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 344));
            grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));

            // ---- connection, across the top
            Card conn = new Card(null);
            conn.Dock = DockStyle.Fill;
            conn.Margin = new Padding(0, 0, 0, 10);
            conn.Padding = new Padding(14, 10, 14, 10);

            conn.Controls.Add(Theme.MakeLabel("Port", Theme.UiFont, Theme.TextDim, 4, 8));

            _portBox = new ComboBox();
            _portBox.DropDownStyle = ComboBoxStyle.DropDownList;
            _portBox.SetBounds(46, 4, 150, 24);
            StyleCombo(_portBox);
            conn.Controls.Add(_portBox);

            _refreshButton = new Button();
            _refreshButton.Text = "Refresh";
            _refreshButton.SetBounds(204, 3, 76, 26);
            _refreshButton.Click += delegate { RefreshPorts(); };
            Theme.StyleButton(_refreshButton);
            conn.Controls.Add(_refreshButton);

            _connectButton = new Button();
            _connectButton.Text = "Connect";
            _connectButton.SetBounds(288, 3, 100, 26);
            _connectButton.Click += delegate { ToggleConnection(); };
            Theme.StylePrimary(_connectButton);
            conn.Controls.Add(_connectButton);

            _status = Theme.MakeLabel("Not connected.", Theme.UiFontSmall, Theme.TextDim, 400, 10);
            conn.Controls.Add(_status);

            _feedVJoy = new CheckBox();
            _feedVJoy.Text = "Feed vJoy (bridge boards)";
            _feedVJoy.SetBounds(4, 36, 220, 20);
            _feedVJoy.ForeColor = Theme.Text;
            _feedVJoy.BackColor = Color.Transparent;
            _feedVJoy.CheckedChanged += delegate { ToggleVJoy(); };
            conn.Controls.Add(_feedVJoy);

            _vjoyStatus = Theme.MakeLabel("", Theme.UiFontSmall, Theme.TextFaint, 230, 38);
            _vjoyStatus.MaximumSize = new Size(700, 0);
            conn.Controls.Add(_vjoyStatus);

            grid.Controls.Add(conn, 0, 0);
            grid.SetColumnSpan(conn, 2);

            // ---- settings
            Card settingsCard = new Card("Wheel settings");
            settingsCard.Dock = DockStyle.Fill;
            settingsCard.Margin = new Padding(0, 0, 10, 10);

            _settingsBody = new Panel();
            _settingsBody.Dock = DockStyle.Fill;
            _settingsBody.BackColor = Theme.Panel;
            _settingsBody.AutoScroll = true;
            settingsCard.Controls.Add(_settingsBody);

            Panel settings = _settingsBody;
            int y = 4;

            settings.Controls.Add(Theme.MakeLabel("Encoder PPR", Theme.UiFont, Theme.TextDim, 4, y + 3));
            _ppr = MakeNumeric(1, 20000, 600);
            _ppr.SetBounds(140, y, 90, 22);
            settings.Controls.Add(_ppr);

            _measurePpr = new Button();
            _measurePpr.Text = "Measure";
            _measurePpr.SetBounds(238, y - 2, 90, 26);
            Theme.StyleButton(_measurePpr);
            _measurePpr.Click += delegate { MeasurePpr(); };
            settings.Controls.Add(_measurePpr);
            y += 28;

            Label pprHint = Theme.MakeLabel(
                "Before quadrature: a 600 P/R encoder is 600.",
                Theme.UiFontSmall, Theme.TextFaint, 140, y);
            settings.Controls.Add(pprHint);
            y += 24;

            settings.Controls.Add(Theme.MakeLabel("Rotation", Theme.UiFont, Theme.TextDim, 4, y + 3));
            _rotation = MakeNumeric(90, 2000, 900);
            _rotation.SetBounds(140, y, 90, 22);
            settings.Controls.Add(_rotation);
            settings.Controls.Add(Theme.MakeLabel("degrees lock to lock", Theme.UiFontSmall, Theme.TextFaint, 238, y + 4));
            y += 32;

            _invert = new CheckBox();
            _invert.Text = "Invert steering direction";
            _invert.SetBounds(140, y, 240, 20);
            _invert.ForeColor = Theme.Text;
            _invert.BackColor = Color.Transparent;
            settings.Controls.Add(_invert);
            y += 28;

            settings.Controls.Add(Theme.MakeLabel("Driver", Theme.UiFont, Theme.TextDim, 4, y + 3));
            _driver = new ComboBox();
            _driver.DropDownStyle = ComboBoxStyle.DropDownList;
            _driver.Items.Add("Dual PWM  (IBT-2 / BTS7960)");
            _driver.Items.Add("PWM + direction");
            _driver.Items.Add("PWM + direction + enable");
            _driver.SelectedIndex = 0;
            _driver.SetBounds(140, y, 230, 24);
            StyleCombo(_driver);
            settings.Controls.Add(_driver);
            y += 34;

            settings.Controls.Add(Theme.MakeLabel("Torque limit", Theme.UiFont, Theme.TextDim, 4, y + 4));
            _maxTorque = new Slider();
            _maxTorque.Minimum = 0;
            _maxTorque.Maximum = 100;
            _maxTorque.Value = 30;
            _maxTorque.SetBounds(140, y, 190, 26);
            _maxTorque.BackColor = Theme.Panel;
            _maxTorque.ValueChanged += delegate {
                _maxTorqueLabel.Text = _maxTorque.Value + "%";
            };
            settings.Controls.Add(_maxTorque);
            _maxTorqueLabel = Theme.MakeLabel("30%", Theme.MonoFontSmall, Theme.Text, 338, y + 5);
            settings.Controls.Add(_maxTorqueLabel);
            y += 30;

            settings.Controls.Add(Theme.MakeLabel("Game force", Theme.UiFont, Theme.TextDim, 4, y + 4));
            _ffbGain = new Slider();
            _ffbGain.Minimum = 0;
            _ffbGain.Maximum = 100;
            _ffbGain.Value = 100;
            _ffbGain.SetBounds(140, y, 190, 26);
            _ffbGain.BackColor = Theme.Panel;
            _ffbGain.ValueChanged += delegate { _ffbGainLabel.Text = _ffbGain.Value + "%"; };
            settings.Controls.Add(_ffbGain);
            _ffbGainLabel = Theme.MakeLabel("100%", Theme.MonoFontSmall, Theme.Text, 338, y + 5);
            settings.Controls.Add(_ffbGainLabel);
            y += 30;

            _ffbInvert = new CheckBox();
            _ffbInvert.Text = "Invert force direction";
            _ffbInvert.SetBounds(140, y, 240, 20);
            _ffbInvert.ForeColor = Theme.Text;
            _ffbInvert.BackColor = Color.Transparent;
            settings.Controls.Add(_ffbInvert);
            y += 22;

            Label signHint = Theme.MakeLabel(
                "Tick this if the wheel fights you instead of centring.",
                Theme.UiFontSmall, Theme.TextFaint, 140, y);
            settings.Controls.Add(signHint);
            y += 26;

            _readButton = MakeButton(settings, "Read", 4, y, 66, false);
            _readButton.Click += delegate { _link.Send("?"); };

            _applyButton = MakeButton(settings, "Apply", 76, y, 66, true);
            _applyButton.Click += delegate { ApplySettings(); };

            _saveButton = MakeButton(settings, "Save to board", 148, y, 110, false);
            _saveButton.Click += delegate { ApplySettings(); _link.Send("SAVE"); };

            _centreButton = MakeButton(settings, "Set centre", 264, y, 96, false);
            _centreButton.Click += delegate { _link.Send("CENTER"); };
            y += 42;

            // --- pin map. Nothing here is baked into the firmware any more, so
            // an encoder or driver wired to whatever was convenient still works.
            settings.Controls.Add(Theme.MakeLabel("Hardware", Theme.UiFontBold, Theme.Text, 4, y));
            y += 24;

            settings.Controls.Add(Theme.MakeLabel("Steering from", Theme.UiFont, Theme.TextDim, 4, y + 3));
            _encType = new ComboBox();
            _encType.DropDownStyle = ComboBoxStyle.DropDownList;
            _encType.Items.Add("Quadrature encoder (A/B)");
            _encType.Items.Add("Potentiometer");
            _encType.SelectedIndex = 0;
            _encType.SetBounds(140, y, 230, 24);
            StyleCombo(_encType);
            _encType.SelectedIndexChanged += delegate { UpdateEnabled(); };
            settings.Controls.Add(_encType);
            y += 30;

            settings.Controls.Add(Theme.MakeLabel("Encoder pins", Theme.UiFont, Theme.TextDim, 4, y + 3));
            _encA = MakePin(settings, 140, y, "A");
            _encB = MakePin(settings, 202, y, "B");

            _findPins = new Button();
            _findPins.Text = "Find pins";
            _findPins.SetBounds(266, y - 2, 100, 26);
            Theme.StyleButton(_findPins);
            _findPins.Click += delegate { StartPinScan(); };
            settings.Controls.Add(_findPins);
            y += 26;

            settings.Controls.Add(Theme.MakeLabel(
                "Not sure where it is wired? Press Find pins and turn the wheel.",
                Theme.UiFontSmall, Theme.TextFaint, 140, y));
            y += 24;

            settings.Controls.Add(Theme.MakeLabel("Motor pins", Theme.UiFont, Theme.TextDim, 4, y + 3));
            _motorA = MakePin(settings, 140, y, "PWM");
            _motorB = MakePin(settings, 202, y, "PWM2");
            _motorDir = MakePin(settings, 264, y, "DIR");
            _motorEn = MakePin(settings, 326, y, "EN");
            y += 22;
            settings.Controls.Add(Theme.MakeLabel("PWM / PWM2 / DIR / ENABLE", Theme.UiFontSmall, Theme.TextFaint, 140, y));
            y += 24;

            settings.Controls.Add(Theme.MakeLabel("Pedal pins", Theme.UiFont, Theme.TextDim, 4, y + 3));
            _pedalA = MakePin(settings, 140, y, "thr");
            _pedalB = MakePin(settings, 202, y, "brk");
            _pedalC = MakePin(settings, 264, y, "clu");
            y += 22;
            settings.Controls.Add(Theme.MakeLabel("throttle / brake / clutch, analogue pin numbers",
                Theme.UiFontSmall, Theme.TextFaint, 140, y));
            y += 26;

            Label pinHint = Theme.MakeLabel(
                "Analogue pins are numbered A0 = 14, A1 = 15, A2 = 16 on an Uno or Nano.\r\n"
              + "On an Uno the encoder can now go on ANY two pins, not just D2 and D3.",
                Theme.UiFontSmall, Theme.TextFaint, 4, y);
            settings.Controls.Add(pinHint);

            grid.Controls.Add(settingsCard, 0, 1);

            // ---- force feedback test
            Card ffb = new Card("Motor test");
            ffb.Dock = DockStyle.Fill;
            ffb.Margin = new Padding(0, 0, 0, 10);

            y = 46;

            Label ffbWarn = Theme.MakeLabel(
                "This turns the motor. Keep clear of the wheel and have the motor\r\n"
              + "supply where you can cut it. Effects stop by themselves if the app\r\n"
              + "stops talking to the board.",
                Theme.UiFontSmall, Theme.Warn, 4, y);
            ffb.Controls.Add(ffbWarn);
            y += 52;

            _armMotor = new CheckBox();
            _armMotor.Text = "Arm motor output";
            _armMotor.SetBounds(4, y, 200, 20);
            _armMotor.ForeColor = Theme.Text;
            _armMotor.Font = Theme.UiFontBold;
            _armMotor.BackColor = Color.Transparent;
            _armMotor.CheckedChanged += delegate
            {
                if (!_armMotor.Checked) StopEffect();
                UpdateEnabled();
            };
            ffb.Controls.Add(_armMotor);
            y += 30;

            ffb.Controls.Add(Theme.MakeLabel("Strength", Theme.UiFont, Theme.TextDim, 4, y + 4));
            _strength = new Slider();
            _strength.Minimum = 0;
            _strength.Maximum = 100;
            _strength.Value = 25;
            _strength.SetBounds(90, y, 190, 26);
            _strength.BackColor = Theme.Panel;
            _strength.ValueChanged += delegate
            {
                _strengthLabel.Text = _strength.Value + "%";
                RefreshRunningEffect();
            };
            ffb.Controls.Add(_strength);
            _strengthLabel = Theme.MakeLabel("25%", Theme.MonoFontSmall, Theme.Text, 288, y + 5);
            ffb.Controls.Add(_strengthLabel);
            y += 36;

            string[][] effects = new string[][]
            {
                new string[] { "Centring spring", "SPRING" },
                new string[] { "Push left",       "CONST-" },
                new string[] { "Push right",      "CONST+" },
                new string[] { "Damper",          "DAMPER" },
                new string[] { "Friction",        "FRICTION" },
                new string[] { "Vibrate",         "SINE" }
            };

            _effectButtons = new Button[effects.Length];
            for (int i = 0; i < effects.Length; i++)
            {
                int col = i % 2, rowIdx = i / 2;
                Button eb = new Button();
                eb.Text = effects[i][0];
                eb.Tag = effects[i][1];
                eb.SetBounds(4 + col * 152, y + rowIdx * 32, 144, 28);
                Theme.StyleButton(eb);
                eb.Click += delegate(object s, EventArgs e) { StartEffect((string)((Button)s).Tag); };
                ffb.Controls.Add(eb);
                _effectButtons[i] = eb;
            }
            y += ((effects.Length + 1) / 2) * 32 + 8;

            _stopButton = new Button();
            _stopButton.Text = "STOP";
            _stopButton.SetBounds(4, y, 144, 34);
            Theme.StyleButton(_stopButton);
            _stopButton.BackColor = Color.FromArgb(0x6A, 0x22, 0x1E);
            _stopButton.ForeColor = Color.FromArgb(0xFF, 0xD8, 0xD5);
            _stopButton.FlatAppearance.BorderColor = Theme.Bad;
            _stopButton.Font = Theme.UiFontBold;
            _stopButton.Click += delegate { StopEffect(); };
            ffb.Controls.Add(_stopButton);

            _effectStatus = Theme.MakeLabel("", Theme.UiFontSmall, Theme.TextDim, 156, y + 4);
            ffb.Controls.Add(_effectStatus);

            _ffbLabel = Theme.MakeLabel("", Theme.UiFontSmall, Theme.TextFaint, 156, y + 20);
            _ffbLabel.MaximumSize = new Size(220, 0);
            ffb.Controls.Add(_ffbLabel);

            grid.Controls.Add(ffb, 1, 1);

            // ---- live readout and log
            Card live = new Card("Live");
            live.Dock = DockStyle.Fill;

            _dial = new SteeringDial();
            _dial.Dock = DockStyle.Left;
            _dial.Width = 200;
            _dial.BackColor = Theme.Panel;
            live.Controls.Add(_dial);

            _log = new TextBox();
            _log.Dock = DockStyle.Fill;
            _log.Multiline = true;
            _log.ReadOnly = true;
            _log.ScrollBars = ScrollBars.Vertical;
            _log.BackColor = Theme.Window;
            _log.ForeColor = Theme.TextDim;
            _log.Font = Theme.MonoFontSmall;
            _log.BorderStyle = BorderStyle.None;
            live.Controls.Add(_log);

            _liveLabel = Theme.MakeLabel("", Theme.MonoFontSmall, Theme.Text, 0, 0);
            _liveLabel.Dock = DockStyle.Top;
            _liveLabel.AutoSize = false;
            _liveLabel.Height = 20;
            live.Controls.Add(_liveLabel);

            _log.BringToFront();
            _liveLabel.BringToFront();
            _dial.BringToFront();

            grid.Controls.Add(live, 0, 2);
            grid.SetColumnSpan(live, 2);

            Controls.Add(grid);
        }

        private static void StyleCombo(ComboBox c)
        {
            c.BackColor = Theme.PanelHi;
            c.ForeColor = Theme.Text;
            c.FlatStyle = FlatStyle.Flat;
        }

        private NumericUpDown MakePin(Control host, int x, int y, string tip)
        {
            NumericUpDown n = MakeNumeric(-1, 127, -1);
            n.SetBounds(x, y, 56, 22);
            host.Controls.Add(n);
            return n;
        }

        private static void SetPin(NumericUpDown n, int value)
        {
            if (n == null) return;
            if (value < n.Minimum) value = (int)n.Minimum;
            if (value > n.Maximum) value = (int)n.Maximum;
            n.Value = value;
        }

        private NumericUpDown MakeNumeric(int min, int max, int value)
        {
            NumericUpDown n = new NumericUpDown();
            n.Minimum = min;
            n.Maximum = max;
            n.Value = value;
            n.BackColor = Theme.PanelHi;
            n.ForeColor = Theme.Text;
            n.BorderStyle = BorderStyle.FixedSingle;
            return n;
        }

        private Button MakeButton(Control host, string text, int x, int y, int w, bool primary)
        {
            Button b = new Button();
            b.Text = text;
            b.SetBounds(x, y, w, 28);
            if (primary) Theme.StylePrimary(b); else Theme.StyleButton(b);
            host.Controls.Add(b);
            return b;
        }

        // =====================================================================
        //  Connection
        // =====================================================================

        private void RefreshPorts()
        {
            string previous = _portBox.SelectedItem as string;
            _portBox.Items.Clear();
            foreach (string p in ToolLocator.SerialPorts()) _portBox.Items.Add(p);

            if (previous != null && _portBox.Items.Contains(previous))
                _portBox.SelectedItem = previous;
            else if (_portBox.Items.Count > 0)
                _portBox.SelectedIndex = _portBox.Items.Count - 1;

            _portBox.Enabled = _portBox.Items.Count > 0;
            UpdateEnabled();
        }

        private void ToggleConnection()
        {
            if (_link.IsOpen)
            {
                StopEffect();
                _poll.Stop();
                _configTimer.Stop();
                _scanTimer.Stop();
                _configReceived = false;
                _pprMeasuring = false;
                if (_measurePpr != null) _measurePpr.Text = "Measure";
                _link.Close();
                if (BridgeDisconnected != null) BridgeDisconnected();
                SetStatus("Disconnected.", Theme.TextDim);
                UpdateEnabled();
                return;
            }

            string port = _portBox.SelectedItem as string;
            string error = _link.Open(port);
            if (error != null)
            {
                SetStatus(error, Theme.Bad);
                return;
            }

            Append("connected to " + port);
            SetStatus("Connected to " + port + ".", Theme.Good);
            UpdateEnabled();

            if (BridgeConnected != null) BridgeConnected(_link);

            _configReceived = false;
            _configTries = 0;
            _configTimer.Start();

            _poll.Start();
        }

        private void OnLinkClosed(string reason)
        {
            if (IsDisposed || !IsHandleCreated) return;
            try
            {
                BeginInvoke((MethodInvoker)delegate
                {
                    _poll.Stop();
                    _link.Close();
                    if (BridgeDisconnected != null) BridgeDisconnected();
                    SetStatus(reason, Theme.Bad);
                    Append(reason);
                    UpdateEnabled();
                });
            }
            catch (InvalidOperationException)
            {
            }
        }

        // Arrives on the serial reader thread.
        private void OnLine(string line)
        {
            if (IsDisposed || !IsHandleCreated) return;
            try
            {
                BeginInvoke((MethodInvoker)delegate { HandleLine(line); });
            }
            catch (InvalidOperationException)
            {
            }
        }

        private void HandleLine(string line)
        {
            if (line.StartsWith("status "))
            {
                ShowStatus(line);
                return;      // 5 Hz of these would bury the log
            }

            if (line.StartsWith("pins "))
            {
                HandlePinsLine(line);
                return;      // 25 a second, and only the total matters
            }

            if (line.StartsWith("--- end"))
            {
                // The whole settings dump has arrived, so the pin fields now
                // hold the board's real values and Apply is safe.
                _configReceived = true;
                _configTimer.Stop();
                UpdateEnabled();
                Append("settings read from the board");
                return;
            }

            Append(line);

            int eq = line.IndexOf('=');
            if (eq <= 0) return;

            string key = line.Substring(0, eq).Trim();
            string value = line.Substring(eq + 1).Trim();
            int number;
            if (!int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out number))
                return;

            // Reflect what the board actually holds, without echoing it back.
            _suppress = true;
            try
            {
                switch (key)
                {
                    case "ppr":
                        if (number >= _ppr.Minimum && number <= _ppr.Maximum) _ppr.Value = number;
                        break;
                    case "rotation":
                        if (number >= _rotation.Minimum && number <= _rotation.Maximum) _rotation.Value = number;
                        break;
                    case "invert":
                        _invert.Checked = number != 0;
                        break;
                    case "driver":
                        if (number >= 0 && number < _driver.Items.Count)
                            _driver.SelectedIndex = number;
                        break;
                    case "maxtorque":
                        _maxTorque.Value = number;
                        _maxTorqueLabel.Text = number + "%";
                        break;
                    case "motor":
                        _hasMotor = number != 0;
                        UpdateEnabled();
                        break;
                    case "ffbgain":
                        _ffbGain.Value = number;
                        _ffbGainLabel.Text = number + "%";
                        break;
                    case "ffbinvert":
                        _ffbInvert.Checked = number != 0;
                        break;
                    case "enctype":
                        _encType.SelectedIndex = number == 1 ? 1 : 0;
                        break;
                    case "enca": SetPin(_encA, number); break;
                    case "encb": SetPin(_encB, number); break;
                    case "motora": SetPin(_motorA, number); break;
                    case "motorb": SetPin(_motorB, number); break;
                    case "motordir": SetPin(_motorDir, number); break;
                    case "motoren": SetPin(_motorEn, number); break;
                    case "pedala": SetPin(_pedalA, number); break;
                    case "pedalb": SetPin(_pedalB, number); break;
                    case "pedalc": SetPin(_pedalC, number); break;
                    case "ffb":
                        _hasFfb = number != 0;
                        SetFfbStatus();
                        break;
                    case "ffbactuators":
                        _gameHasControl = number != 0;
                        SetFfbStatus();
                        break;
                }
            }
            finally
            {
                _suppress = false;
            }
        }

        private bool _hasMotor = true;
        private bool _hasFfb = false;
        private bool _gameHasControl = false;

        // The board reports whether a game has enabled its actuators. That is
        // the difference between "force feedback is wired up" and "a sim is
        // actually driving this right now", and it is worth showing plainly.
        private void SetFfbStatus()
        {
            if (_ffbLabel == null) return;

            if (!_hasFfb)
            {
                _ffbLabel.Text = "Bridge board: motor test only, no force feedback in games.";
                _ffbLabel.ForeColor = Theme.TextFaint;
            }
            else if (_gameHasControl)
            {
                _ffbLabel.Text = "A game has taken force feedback control of this wheel.";
                _ffbLabel.ForeColor = Theme.Good;
            }
            else
            {
                _ffbLabel.Text = "Force feedback ready. No game has claimed it yet.";
                _ffbLabel.ForeColor = Theme.TextDim;
            }
        }

        private long _lastCounts;

        private void ShowStatus(string line)
        {
            _liveLabel.Text = line.Substring(7);

            int c = line.IndexOf("counts=");
            if (c >= 0)
            {
                int stop = line.IndexOf(' ', c);
                string countsText = stop < 0 ? line.Substring(c + 7)
                                             : line.Substring(c + 7, stop - c - 7);
                long v;
                if (long.TryParse(countsText, NumberStyles.Integer, CultureInfo.InvariantCulture, out v))
                    _lastCounts = v;
            }

            // status angle=<n> counts=<n> ...
            int a = line.IndexOf("angle=");
            if (a < 0) return;
            int end = line.IndexOf(' ', a);
            string num = end < 0 ? line.Substring(a + 6) : line.Substring(a + 6, end - a - 6);

            int degrees;
            if (!int.TryParse(num, NumberStyles.Integer, CultureInfo.InvariantCulture, out degrees))
                return;

            int range = (int)_rotation.Value;
            _dial.RangeDegrees = range;
            _dial.Value = 0.5 + (double)degrees / range;
        }

        // =====================================================================
        //  vJoy
        // =====================================================================

        // Bridge boards send their state here; this is what turns it into a
        // device Windows can see. Called on the serial reader thread, so it
        // must not touch the UI.
        private void OnBridgeInput(WheelInput input)
        {
            _vjoy.Feed(input);
        }

        private void ToggleVJoy()
        {
            if (!_feedVJoy.Checked)
            {
                _vjoy.Release();
                SetVJoyStatus("", Theme.TextFaint);
                return;
            }

            string error = _vjoy.Acquire(1);
            if (error != null)
            {
                _suppress = true;
                _feedVJoy.Checked = false;
                _suppress = false;
                SetVJoyStatus(error, Theme.Bad);
                Append(error);
                return;
            }

            SetVJoyStatus("Feeding vJoy device 1 -- steering on X, pedals on Y/Z/Rx.", Theme.Good);
            Append("acquired vJoy device 1");
        }

        private void SetVJoyStatus(string text, Color colour)
        {
            _vjoyStatus.Text = text;
            _vjoyStatus.ForeColor = colour;
        }

        // =====================================================================
        //  Settings and effects
        // =====================================================================

        // Counting the pulses over one known turn is the only reliable way to
        // learn an encoder's PPR. The number printed on the part is often the
        // pre-quadrature figure, often missing, and on a cheap module often
        // simply wrong.
        private void MeasurePpr()
        {
            if (!_link.IsOpen) return;

            if (!_pprMeasuring)
            {
                if (MessageBox.Show(this,
                        "Put a mark on the wheel so you can see where it starts." + "\r\n\r\n"
                      + "Press OK, turn it EXACTLY one full revolution, then press Measure again.",
                        "Measure encoder PPR",
                        MessageBoxButtons.OKCancel, MessageBoxIcon.Information) != DialogResult.OK)
                    return;

                _link.Send("CENTER");
                _pprStartCounts = 0;
                _pprMeasuring = true;
                _measurePpr.Text = "Done";
                SetStatus("Turn exactly one full revolution, then press Done.", Theme.Warn);
                return;
            }

            _pprMeasuring = false;
            _measurePpr.Text = "Measure";

            long counts = Math.Abs(_lastCounts - _pprStartCounts);
            if (counts < 4)
            {
                SetStatus("Only " + counts + " counts seen -- was the wheel turned?", Theme.Bad);
                return;
            }

            // Four counts per pulse, because both edges of both channels count.
            long ppr = counts / 4;
            if (ppr < 1) ppr = 1;
            if (ppr > _ppr.Maximum) ppr = (long)_ppr.Maximum;

            _ppr.Value = ppr;
            SetStatus("Measured " + counts + " counts per turn, so PPR = " + ppr
                      + ". Press Apply, then Save to board.", Theme.Good);
            Append("measured ppr=" + ppr + " from " + counts + " counts");
        }

        // ---- pin finder ---------------------------------------------------

        private void StartPinScan()
        {
            if (!_link.IsOpen) return;

            if (MessageBox.Show(this,
                    "Turn the wheel back and forth for the whole fifteen seconds.\r\n\r\n"
                  + "WheelForge watches every pin on the board and reports which ones move. "
                  + "Buttons and the motor are paused while it runs.\r\n\r\n"
                  + "Ready?",
                    "Find encoder pins",
                    MessageBoxButtons.OKCancel, MessageBoxIcon.Information) != DialogResult.OK)
                return;

            _scanHaveMask = false;
            _scanChanged = 0;
            _scanSamples = 0;
            _scanPinCount = 0;
            _scanEnds = DateTime.UtcNow.AddSeconds(15);

            _poll.Stop();
            _link.Send("SCAN 1");
            _scanTimer.Start();

            _findPins.Enabled = false;
            SetStatus("Turn the wheel now...", Theme.Warn);
            Append("pin scan started");
        }

        private void OnScanTick(object sender, EventArgs e)
        {
            if (!_link.IsOpen) { FinishPinScan(); return; }

            if (DateTime.UtcNow >= _scanEnds) { FinishPinScan(); return; }

            _link.Send("PINS");
            int left = (int)(_scanEnds - DateTime.UtcNow).TotalSeconds + 1;
            SetStatus("Keep turning... " + left + "s", Theme.Warn);
        }

        private void FinishPinScan()
        {
            _scanTimer.Stop();
            _findPins.Enabled = true;

            if (_link.IsOpen)
            {
                _link.Send("SCAN 0");
                _poll.Start();
            }

            List<int> movers = new List<int>();
            for (int i = 0; i < _scanPinCount; i++)
            {
                if (i <= 1) continue;              // the serial link, always busy
                if ((_scanChanged & (1u << i)) != 0) movers.Add(i);
            }

            Append("pin scan finished: " + _scanSamples + " samples, "
                   + movers.Count + " pin(s) moved");

            if (_scanSamples < 5)
            {
                SetStatus("The board did not answer. It may need reflashing with the newest "
                          + "firmware.", Theme.Bad);
                return;
            }

            if (movers.Count == 0)
            {
                SetStatus("No pin moved at all.", Theme.Bad);
                MessageBox.Show(this,
                    "Not one pin changed while you turned the wheel.\r\n\r\n"
                  + "The encoder is not reaching the board at all, so this is wiring rather "
                  + "than pin numbers. In order of likelihood:\r\n\r\n"
                  + "1.  GND is not connected between the encoder and the board.\r\n"
                  + "2.  + is not connected to 5V.\r\n"
                  + "3.  CLK and DT are not actually landing on a pin -- a loose jumper, the "
                  + "wrong header row, or a dead breadboard rail.\r\n"
                  + "4.  The encoder module is faulty.\r\n\r\n"
                  + "A quick check: with a multimeter on continuity, CLK to GND should beep on "
                  + "and off as you turn it.",
                    "Nothing moved", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            string found = "";
            foreach (int pin in movers) found += (found.Length > 0 ? ", " : "") + "D" + pin;

            if (movers.Count == 1)
            {
                SetStatus("Only " + found + " moved. A quadrature encoder needs two.", Theme.Warn);
                MessageBox.Show(this,
                    "Only one pin moved: " + found + ".\r\n\r\n"
                  + "A quadrature encoder has two channels, so one of the two wires is not "
                  + "getting through. Check the other channel.",
                    "One pin moved", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            SetStatus("Found the encoder on " + found + ".", Theme.Good);

            if (MessageBox.Show(this,
                    "These pins moved while you turned the wheel:\r\n\r\n    " + found + "\r\n\r\n"
                  + "Set the encoder to D" + movers[0] + " and D" + movers[1] + "?",
                    "Found the encoder",
                    MessageBoxButtons.YesNo, MessageBoxIcon.Information) != DialogResult.Yes)
                return;

            SetPin(_encA, movers[0]);
            SetPin(_encB, movers[1]);
            ApplySettings();
            Append("encoder set to D" + movers[0] + " and D" + movers[1]
                   + " -- press Save to board to keep it");
        }

        private void HandlePinsLine(string line)
        {
            string[] parts = line.Split(' ');
            if (parts.Length < 3) return;

            int count;
            uint mask;
            if (!int.TryParse(parts[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out count)) return;
            if (!uint.TryParse(parts[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out mask)) return;

            _scanPinCount = count;
            _scanSamples++;

            if (_scanHaveMask) _scanChanged |= (mask ^ _scanLastMask);
            _scanLastMask = mask;
            _scanHaveMask = true;
        }

        private void OnConfigTick(object sender, EventArgs e)
        {
            if (!_link.IsOpen || _configReceived) { _configTimer.Stop(); return; }

            if (_configTries++ > 12)
            {
                _configTimer.Stop();
                SetStatus("The board is connected but did not report its settings. "
                        + "It may be running older firmware.", Theme.Warn);
                UpdateEnabled();
                return;
            }

            _link.Send("?");
        }

        private void ApplySettings()
        {
            if (!_link.IsOpen || _suppress) return;

            // Writing a pin map that was never read from the board would send
            // the -1 the fields start at, which switches the encoder and every
            // pedal off. That is precisely how a working wheel gets broken by
            // pressing Apply.
            if (!_configReceived)
            {
                SetStatus("Waiting for the board to report its settings -- "
                        + "nothing applied.", Theme.Warn);
                return;
            }

            _link.Send("PPR " + (int)_ppr.Value);
            _link.Send("ROT " + (int)_rotation.Value);
            _link.Send("INV " + (_invert.Checked ? 1 : 0));
            _link.Send("DRIVER " + _driver.SelectedIndex);
            _link.Send("MAXTORQUE " + _maxTorque.Value);
            _link.Send("FFBGAIN " + _ffbGain.Value);
            _link.Send("FFBINV " + (_ffbInvert.Checked ? 1 : 0));
            _link.Send("ENCTYPE " + _encType.SelectedIndex);
            _link.Send("ENCPINS " + (int)_encA.Value + " " + (int)_encB.Value);
            _link.Send("MOTORPINS " + (int)_motorA.Value + " " + (int)_motorB.Value
                       + " " + (int)_motorDir.Value + " " + (int)_motorEn.Value);
            _link.Send("PEDALPINS " + (int)_pedalA.Value + " " + (int)_pedalB.Value
                       + " " + (int)_pedalC.Value);
            Append("applied settings (not saved until you press Save)");
        }

        private string _runningEffect;

        private void StartEffect(string kind)
        {
            if (!_link.IsOpen || !_armMotor.Checked) return;

            // The ceiling is enforced on the board as well; sending it again
            // here means a slider moved since the last Apply still counts.
            _link.Send("MAXTORQUE " + _maxTorque.Value);

            int strength = _strength.Value;
            string command;

            switch (kind)
            {
                case "CONST-": command = "TEST CONST -" + strength; break;
                case "CONST+": command = "TEST CONST " + strength; break;
                case "SINE":   command = "TEST SINE " + strength + " 8"; break;
                default:       command = "TEST " + kind + " " + strength; break;
            }

            _runningEffect = command;
            _link.StartEffect(command);
            _effectStatus.Text = "running: " + command;
            _effectStatus.ForeColor = Theme.Warn;
            Append("> " + command);
        }

        private void RefreshRunningEffect()
        {
            if (_runningEffect == null || !_link.IsOpen) return;

            // Re-issue the current effect at the new strength rather than
            // making the user stop and start it again.
            string kind = _runningEffect.Split(' ')[1];
            bool negative = _runningEffect.Contains(" -");
            StartEffect(kind == "CONST" ? (negative ? "CONST-" : "CONST+") : kind);
        }

        private void StopEffect()
        {
            _runningEffect = null;
            if (_link.IsOpen) _link.StopEffect();
            _effectStatus.Text = "stopped";
            _effectStatus.ForeColor = Theme.TextDim;
        }

        // =====================================================================

        private void UpdateEnabled()
        {
            bool open = _link.IsOpen;

            _connectButton.Text = open ? "Disconnect" : "Connect";
            _connectButton.Enabled = open || _portBox.Items.Count > 0;
            _portBox.Enabled = !open && _portBox.Items.Count > 0;
            _refreshButton.Enabled = !open;

            foreach (Control c in new Control[] { _readButton, _centreButton })
                if (c != null) c.Enabled = open;

            // Everything that writes a setting waits until the board has told
            // us what its settings actually are.
            bool ready = open && _configReceived;
            foreach (Control c in new Control[] { _ppr, _rotation, _invert, _driver,
                                                  _maxTorque, _ffbGain, _ffbInvert,
                                                  _applyButton, _saveButton,
                                                  _encType, _encA, _motorA, _motorB,
                                                  _motorDir, _motorEn,
                                                  _pedalA, _pedalB, _pedalC, _findPins,
                                                  _measurePpr })
                if (c != null) c.Enabled = ready;

            // Encoder B is meaningless for a potentiometer.
            if (_encB != null)
                _encB.Enabled = open && _encType != null && _encType.SelectedIndex == 0;

            _armMotor.Enabled = open && _hasMotor;
            if (!_hasMotor) _armMotor.Text = "Arm motor output  (this board has no motor pins)";

            bool armed = open && _armMotor.Checked && _hasMotor;
            foreach (Button b in _effectButtons) b.Enabled = armed;
            _strength.Enabled = armed;
            _stopButton.Enabled = open;
        }

        private void SetStatus(string text, Color colour)
        {
            _status.Text = text;
            _status.ForeColor = colour;
        }

        private void Append(string line)
        {
            if (_log.Lines.Length > 300) _log.Clear();
            _log.AppendText(line + "\r\n");
        }

        // Fills in a port chosen elsewhere, so arriving from the device list
        // lands ready to connect.
        public void Preselect(string portName)
        {
            RefreshPorts();
            if (!string.IsNullOrEmpty(portName) && _portBox.Items.Contains(portName))
                _portBox.SelectedItem = portName;
            UpdateEnabled();
        }

        // Called when something else needs this port -- flashing, mainly.
        public void ReleaseIfUsing(string portName)
        {
            if (!_link.IsOpen) return;
            if (!string.Equals(_link.PortName, portName, StringComparison.OrdinalIgnoreCase)) return;

            StopEffect();
            _poll.Stop();
            _link.Close();
            SetStatus("Released " + portName + " so it could be flashed.", Theme.Warn);
            Append("released " + portName + " for flashing");
            UpdateEnabled();
        }

        public void ShutDown()
        {
            _scanTimer.Stop();
            _configTimer.Stop();
            _poll.Stop();
            StopEffect();
            _link.Close();
            if (BridgeDisconnected != null) BridgeDisconnected();
            _vjoy.Release();
        }
    }
}
