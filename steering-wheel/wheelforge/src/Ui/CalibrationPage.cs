using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
using WheelForge.Core;
using WheelForge.Hid;

namespace WheelForge.Ui
{
    // Pedal travel, deadzones, curves, centre and saved profiles.
    //
    // All of it is worked out on the PC from values the wheel already reports,
    // so none of it needs a firmware change or a single write to the device.
    // The numbers captured here are also exactly what our own firmware will
    // need to be told later.
    public class CalibrationPage : Panel
    {
        private CalibrationProfile _profile = new CalibrationProfile();
        private HidReportLayout _layout;
        private HidDeviceInfo _device;

        private int[] _liveRaw = new int[0];
        private bool _hasLive;
        private bool _capturing;

        // chrome
        private ListBox _axisList;
        private Label _emptyLabel;
        private Card _settingsCard;
        private Panel _settingsBody;

        private AxisBar _rawBar;
        private AxisBar _calBar;
        private Label _rangeLabel;
        private Button _captureButton;
        private Button _resetButton;
        private Button _centreButton;
        private CheckBox _centredBox;
        private CheckBox _invertBox;
        private Slider _dzCentre;
        private Slider _dzEnds;
        private Slider _gamma;
        private Label _dzCentreLabel;
        private Label _dzEndsLabel;
        private Label _gammaLabel;
        private CurvePlot _plot;

        private Button _minButton;
        private Button _maxButton;
        private Button _quickCentreButton;
        private Label _quickStatus;

        private ComboBox _profileBox;
        private Button _loadButton;
        private Button _saveButton;
        private TextBox _nameBox;
        private Label _profileStatus;

        private bool _suppressEvents;

        public CalibrationPage()
        {
            BackColor = Theme.Window;
            Build();
            RefreshProfileList();
            UpdateEnabled();
        }

        // =====================================================================
        //  Build
        // =====================================================================

        private void Build()
        {
            TableLayoutPanel grid = new TableLayoutPanel();
            grid.Dock = DockStyle.Fill;
            grid.BackColor = Theme.Window;
            grid.ColumnCount = 2;
            grid.RowCount = 3;
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 232));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 116));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 72));
            grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));

            grid.Controls.Add(BuildQuickCard(), 0, 0);
            grid.SetColumnSpan(grid.GetControlFromPosition(0, 0), 2);

            // --- profiles
            Card profileCard = new Card(null);
            profileCard.Dock = DockStyle.Fill;
            profileCard.Margin = new Padding(0, 0, 0, 10);
            profileCard.Padding = new Padding(14, 10, 14, 10);

            Label pl = Theme.MakeLabel("Profile", Theme.UiFontSmall, Theme.TextDim, 2, 6);
            profileCard.Controls.Add(pl);

            _profileBox = new ComboBox();
            _profileBox.DropDownStyle = ComboBoxStyle.DropDownList;
            _profileBox.Location = new Point(50, 2);
            _profileBox.Width = 190;
            _profileBox.BackColor = Theme.PanelHi;
            _profileBox.ForeColor = Theme.Text;
            _profileBox.FlatStyle = FlatStyle.Flat;
            profileCard.Controls.Add(_profileBox);

            _loadButton = new Button();
            _loadButton.Text = "Load";
            _loadButton.SetBounds(248, 1, 64, 24);
            _loadButton.Click += delegate { LoadSelectedProfile(); };
            Theme.StyleButton(_loadButton);
            profileCard.Controls.Add(_loadButton);

            Label nl = Theme.MakeLabel("Save as", Theme.UiFontSmall, Theme.TextDim, 332, 6);
            profileCard.Controls.Add(nl);

            _nameBox = new TextBox();
            _nameBox.Text = "Default";
            _nameBox.SetBounds(388, 2, 170, 22);
            _nameBox.BackColor = Theme.PanelHi;
            _nameBox.ForeColor = Theme.Text;
            _nameBox.BorderStyle = BorderStyle.FixedSingle;
            profileCard.Controls.Add(_nameBox);

            _saveButton = new Button();
            _saveButton.Text = "Save";
            _saveButton.SetBounds(566, 1, 64, 24);
            _saveButton.Click += delegate { SaveProfile(); };
            Theme.StylePrimary(_saveButton);
            profileCard.Controls.Add(_saveButton);

            _profileStatus = Theme.MakeLabel("", Theme.UiFontSmall, Theme.TextFaint, 2, 30);
            profileCard.Controls.Add(_profileStatus);

            grid.Controls.Add(profileCard, 0, 1);
            grid.SetColumnSpan(profileCard, 2);

            // --- axis list
            Card listCard = new Card("Axes");
            listCard.Dock = DockStyle.Fill;
            listCard.Margin = new Padding(0, 0, 10, 0);

            _axisList = new ListBox();
            _axisList.Dock = DockStyle.Fill;
            _axisList.BackColor = Theme.Panel;
            _axisList.ForeColor = Theme.Text;
            _axisList.BorderStyle = BorderStyle.None;
            _axisList.IntegralHeight = false;
            _axisList.ItemHeight = 24;
            // Owner drawn, because a plain ListBox paints its selection in the
            // Windows highlight colour regardless of BackColor.
            _axisList.DrawMode = DrawMode.OwnerDrawFixed;
            _axisList.DrawItem += DrawAxisItem;
            _axisList.SelectedIndexChanged += delegate { OnAxisSelected(); };
            listCard.Controls.Add(_axisList);
            grid.Controls.Add(listCard, 0, 2);

            // --- settings
            _settingsCard = new Card("Settings");
            _settingsCard.Dock = DockStyle.Fill;
            _settingsCard.Margin = new Padding(0, 0, 0, 0);

            _settingsBody = new Panel();
            _settingsBody.Dock = DockStyle.Fill;
            _settingsBody.BackColor = Theme.Panel;
            _settingsBody.AutoScroll = true;
            _settingsCard.Controls.Add(_settingsBody);

            BuildSettings(_settingsBody);
            _settingsBody.Resize += delegate { LayoutSettings(); };

            _emptyLabel = Theme.MakeLabel(
                "Connect a device on the Monitor page, then pick an axis.",
                Theme.UiFont, Theme.TextFaint, 4, 6);
            _settingsBody.Controls.Add(_emptyLabel);

            grid.Controls.Add(_settingsCard, 1, 2);

            Controls.Add(grid);
        }

        // The two button flow, which is how pedal calibration should feel:
        // let everything go, press Min, floor everything, press Max. It writes
        // every pedal at once -- doing them one at a time was busywork.
        private Card BuildQuickCard()
        {
            Card card = new Card("Quick calibration");
            card.Dock = DockStyle.Fill;
            card.Margin = new Padding(0, 0, 0, 10);

            _minButton = new Button();
            _minButton.Text = "1.  Set Min";
            _minButton.SetBounds(16, 48, 132, 34);
            Theme.StylePrimary(_minButton);
            _minButton.Click += delegate { CapturePedals(true); };
            card.Controls.Add(_minButton);

            Label minHint = Theme.MakeLabel("with every pedal released",
                Theme.UiFontSmall, Theme.TextFaint, 18, 84);
            card.Controls.Add(minHint);

            _maxButton = new Button();
            _maxButton.Text = "2.  Set Max";
            _maxButton.SetBounds(158, 48, 132, 34);
            Theme.StylePrimary(_maxButton);
            _maxButton.Click += delegate { CapturePedals(false); };
            card.Controls.Add(_maxButton);

            Label maxHint = Theme.MakeLabel("holding them all to the floor",
                Theme.UiFontSmall, Theme.TextFaint, 160, 84);
            card.Controls.Add(maxHint);

            _quickCentreButton = new Button();
            _quickCentreButton.Text = "Centre the wheel";
            _quickCentreButton.SetBounds(316, 48, 148, 34);
            Theme.StyleButton(_quickCentreButton);
            _quickCentreButton.Click += delegate { CaptureSteeringCentre(); };
            card.Controls.Add(_quickCentreButton);

            Label centreHint = Theme.MakeLabel("with it held straight ahead",
                Theme.UiFontSmall, Theme.TextFaint, 318, 84);
            card.Controls.Add(centreHint);

            _quickStatus = Theme.MakeLabel("Connect a device on the Monitor page first.",
                Theme.UiFontSmall, Theme.TextDim, 486, 58);
            _quickStatus.MaximumSize = new Size(420, 0);
            card.Controls.Add(_quickStatus);

            return card;
        }

        private void CapturePedals(bool isMin)
        {
            if (!_hasLive) { SetQuick("No live data -- connect on the Monitor page.", Theme.Bad); return; }

            int touched = 0;
            for (int i = 0; i < _profile.Axes.Count && i < _liveRaw.Length; i++)
            {
                AxisCalibration cal = _profile.Axes[i];
                if (cal.Centred) continue;              // pedals only
                if (isMin) cal.Min = _liveRaw[i]; else cal.Max = _liveRaw[i];
                touched++;
            }

            if (touched == 0) { SetQuick("No pedal axes on this device.", Theme.Warn); return; }

            if (!isMin) NormalisePedalTravel();

            SetQuick((isMin ? "Min" : "Max") + " captured for " + touched
                     + (touched == 1 ? " axis." : " axes.")
                     + (isMin ? "  Now floor the pedals and press Set Max." : "  Done."),
                     isMin ? Theme.TextDim : Theme.Good);

            RefreshSelectedAxis();
        }

        // A pot wired the other way round reads high at rest, so Max lands below
        // Min. Rather than calling that a mistake, swap the pair and flip the
        // invert flag -- the result is the same pedal, calibrated correctly.
        private void NormalisePedalTravel()
        {
            foreach (AxisCalibration cal in _profile.Axes)
            {
                if (cal.Centred) continue;
                if (cal.Max >= cal.Min) continue;

                int t = cal.Min;
                cal.Min = cal.Max;
                cal.Max = t;
                cal.Invert = !cal.Invert;
            }
        }

        private void CaptureSteeringCentre()
        {
            if (!_hasLive) { SetQuick("No live data -- connect on the Monitor page.", Theme.Bad); return; }

            int touched = 0;
            for (int i = 0; i < _profile.Axes.Count && i < _liveRaw.Length; i++)
            {
                AxisCalibration cal = _profile.Axes[i];
                if (!cal.Centred) continue;
                cal.Centre = _liveRaw[i];
                touched++;
            }

            SetQuick(touched > 0 ? "Centre captured." : "No centred axis on this device.",
                     touched > 0 ? Theme.Good : Theme.Warn);
            RefreshSelectedAxis();
        }

        private void RefreshSelectedAxis()
        {
            AxisCalibration cal = Selected();
            if (cal == null) return;
            UpdateRangeLabel(cal);
            if (_plot != null) _plot.Invalidate();
            _axisList.Invalidate();
            UpdateEnabled();
        }

        private void SetQuick(string text, Color colour)
        {
            if (_quickStatus == null) return;
            _quickStatus.Text = text;
            _quickStatus.ForeColor = colour;
        }

        private void BuildSettings(Panel host)
        {
            int y = 4;

            host.Controls.Add(Theme.MakeLabel("Live", Theme.UiFontBold, Theme.Text, 4, y));
            y += 22;

            _rawBar = new AxisBar();
            _rawBar.SetBounds(4, y, 520, 24);
            _rawBar.BackColor = Theme.Panel;
            _rawBar.Caption = "raw";
            _rawBar.BarColour = Theme.TextDim;
            host.Controls.Add(_rawBar);
            y += 26;

            _calBar = new AxisBar();
            _calBar.SetBounds(4, y, 520, 24);
            _calBar.BackColor = Theme.Panel;
            _calBar.Caption = "calibrated";
            _calBar.BarColour = Theme.Accent;
            host.Controls.Add(_calBar);
            y += 34;

            host.Controls.Add(Theme.MakeLabel("Travel", Theme.UiFontBold, Theme.Text, 4, y));
            y += 22;

            _captureButton = new Button();
            _captureButton.Text = "Start capture";
            _captureButton.SetBounds(4, y, 128, 26);
            _captureButton.Click += delegate { ToggleCapture(); };
            Theme.StylePrimary(_captureButton);
            host.Controls.Add(_captureButton);

            _resetButton = new Button();
            _resetButton.Text = "Reset";
            _resetButton.SetBounds(138, y, 70, 26);
            _resetButton.Click += delegate { ResetTravel(); };
            Theme.StyleButton(_resetButton);
            host.Controls.Add(_resetButton);

            _centreButton = new Button();
            _centreButton.Text = "Set centre here";
            _centreButton.SetBounds(214, y, 124, 26);
            _centreButton.Click += delegate { SetCentreHere(); };
            Theme.StyleButton(_centreButton);
            host.Controls.Add(_centreButton);
            y += 32;

            _rangeLabel = Theme.MakeLabel("", Theme.MonoFontSmall, Theme.TextDim, 4, y);
            host.Controls.Add(_rangeLabel);
            y += 24;

            Label hint = Theme.MakeLabel(
                "Press start, then sweep the axis through its full travel -- lock to lock, or the "
              + "pedal to the floor -- and press stop.",
                Theme.UiFontSmall, Theme.TextFaint, 4, y);
            hint.MaximumSize = new Size(520, 0);
            host.Controls.Add(hint);
            y += 34;

            _centredBox = new CheckBox();
            _centredBox.Text = "Centred axis (steering, not a pedal)";
            _centredBox.SetBounds(4, y, 300, 20);
            _centredBox.ForeColor = Theme.Text;
            _centredBox.BackColor = Color.Transparent;
            _centredBox.CheckedChanged += delegate { OnFieldChanged(); };
            host.Controls.Add(_centredBox);
            y += 24;

            _invertBox = new CheckBox();
            _invertBox.Text = "Invert direction";
            _invertBox.SetBounds(4, y, 300, 20);
            _invertBox.ForeColor = Theme.Text;
            _invertBox.BackColor = Color.Transparent;
            _invertBox.CheckedChanged += delegate { OnFieldChanged(); };
            host.Controls.Add(_invertBox);
            y += 32;

            host.Controls.Add(Theme.MakeLabel("Shaping", Theme.UiFontBold, Theme.Text, 4, y));
            y += 22;

            _dzCentre = MakeSlider(host, ref y, "Centre deadzone", 0, 45, 0, out _dzCentreLabel);
            _dzEnds = MakeSlider(host, ref y, "End deadzone", 0, 45, 0, out _dzEndsLabel);
            _gamma = MakeSlider(host, ref y, "Curve", 50, 250, 100, out _gammaLabel);

            y += 6;
            host.Controls.Add(Theme.MakeLabel("Response", Theme.UiFontBold, Theme.Text, 4, y));
            y += 22;

            _plot = new CurvePlot();
            _plot.SetBounds(4, y, 300, 170);
            _plot.BackColor = Theme.Panel;
            host.Controls.Add(_plot);
        }

        // Widths come from the real client width on every resize. Anchoring
        // inside an AutoScroll panel sets margins before the panel has been
        // sized, which leaves the bars hanging under the scrollbar.
        private void LayoutSettings()
        {
            if (_rawBar == null || _settingsBody == null) return;

            int w = _settingsBody.ClientSize.Width - 12;
            if (_settingsBody.VerticalScroll.Visible) w -= 2;
            if (w < 240) w = 240;

            _rawBar.Width = w;
            _calBar.Width = w;
            if (_plot != null) _plot.Width = Math.Min(w, 460);
        }

        private void DrawAxisItem(object sender, DrawItemEventArgs e)
        {
            if (e.Index < 0) return;

            bool selected = (e.State & DrawItemState.Selected) == DrawItemState.Selected;
            Graphics g = e.Graphics;

            using (Brush b = new SolidBrush(selected ? Theme.PanelHi : Theme.Panel))
                g.FillRectangle(b, e.Bounds);

            if (selected)
            {
                using (Brush b = new SolidBrush(Theme.Accent))
                    g.FillRectangle(b, new Rectangle(e.Bounds.Left, e.Bounds.Top, 3, e.Bounds.Height));
            }

            // A tick against axes whose travel has actually been captured, so
            // it is obvious what is still to do.
            bool captured = false;
            if (e.Index < _profile.Axes.Count && _layout != null && e.Index < _layout.Axes.Count)
            {
                AxisCalibration cal = _profile.Axes[e.Index];
                HidAxis axis = _layout.Axes[e.Index];
                captured = cal.HasTravel
                    && (cal.Min != axis.LogicalMin || cal.Max != axis.LogicalMax);
            }

            if (captured)
            {
                using (Brush b = new SolidBrush(Theme.Good))
                    g.FillEllipse(b, e.Bounds.Right - 16, e.Bounds.Top + e.Bounds.Height / 2 - 3, 6, 6);
            }

            StringFormat sf = new StringFormat();
            sf.LineAlignment = StringAlignment.Center;
            sf.Trimming = StringTrimming.EllipsisCharacter;
            sf.FormatFlags = StringFormatFlags.NoWrap;

            using (Brush b = new SolidBrush(selected ? Theme.Text : Theme.TextDim))
                g.DrawString(_axisList.Items[e.Index].ToString(),
                    selected ? Theme.UiFontBold : Theme.UiFont, b,
                    new RectangleF(e.Bounds.Left + 12, e.Bounds.Top,
                                   e.Bounds.Width - 30, e.Bounds.Height), sf);
        }

        private Slider MakeSlider(Panel host, ref int y, string caption,
                                  int min, int max, int value, out Label readout)
        {
            host.Controls.Add(Theme.MakeLabel(caption, Theme.UiFont, Theme.TextDim, 4, y + 5));

            Slider sl = new Slider();
            sl.SetBounds(130, y, 300, 26);
            sl.Minimum = min;
            sl.Maximum = max;
            sl.Value = value;
            sl.BackColor = Theme.Panel;
            sl.ValueChanged += delegate { OnFieldChanged(); };
            host.Controls.Add(sl);

            readout = Theme.MakeLabel("", Theme.MonoFontSmall, Theme.Text, 442, y + 5);
            host.Controls.Add(readout);

            y += 34;
            return sl;
        }

        // =====================================================================
        //  Device wiring
        // =====================================================================

        public void SetDevice(HidDeviceInfo device, HidReportLayout layout)
        {
            _device = device;
            _layout = layout;
            _capturing = false;
            _hasLive = false;

            _axisList.Items.Clear();

            if (layout == null || device == null)
            {
                _profile.Axes.Clear();
                _liveRaw = new int[0];
                UpdateEnabled();
                return;
            }

            _liveRaw = new int[layout.Axes.Count];
            _profile.DeviceVidPid = device.VidPid;

            // Keep any calibration already loaded for axes this device also has,
            // and default the rest. Swapping profiles must not silently discard
            // work for an axis the new profile happens not to mention.
            List<AxisCalibration> merged = new List<AxisCalibration>();

            for (int i = 0; i < layout.Axes.Count; i++)
            {
                HidAxis a = layout.Axes[i];
                AxisCalibration cal = _profile.Find(a.UsagePage, a.Usage);

                if (cal == null)
                {
                    cal = new AxisCalibration();
                    cal.UsagePage = a.UsagePage;
                    cal.Usage = a.Usage;
                    cal.Min = a.LogicalMin;
                    cal.Max = a.LogicalMax;
                    cal.Centre = a.LogicalMin + (a.LogicalMax - a.LogicalMin) / 2;
                    cal.Centred = IsProbablySteering(a);
                }

                cal.Label = a.Name;
                merged.Add(cal);
                _axisList.Items.Add(a.Name);
            }

            _profile.Axes = merged;

            if (_axisList.Items.Count > 0) _axisList.SelectedIndex = 0;
            UpdateEnabled();
        }

        private static bool IsProbablySteering(HidAxis a)
        {
            if (a.Usage == 0xC8 && UsageNames.IsSimulationUsage(a.UsagePage, a.Usage)) return true;
            return a.UsagePage == 0x01 && a.Usage == 0x30;
        }

        // Called from the UI tick with the newest decoded report.
        public void PushState(HidState state)
        {
            if (state == null || _layout == null) return;
            if (state.AxisRaw.Length != _liveRaw.Length) return;

            Array.Copy(state.AxisRaw, _liveRaw, _liveRaw.Length);
            _hasLive = true;

            int i = _axisList.SelectedIndex;
            if (i < 0 || i >= _profile.Axes.Count) return;

            AxisCalibration cal = _profile.Axes[i];
            int raw = _liveRaw[i];

            if (_capturing)
            {
                bool grew = false;
                if (raw < cal.Min) { cal.Min = raw; grew = true; }
                if (raw > cal.Max) { cal.Max = raw; grew = true; }
                if (grew) { UpdateRangeLabel(cal); _plot.Invalidate(); }
            }

            HidAxis axis = _layout.Axes[i];
            int span = axis.LogicalMax - axis.LogicalMin;
            double rawNorm = span > 0 ? (raw - (double)axis.LogicalMin) / span : 0.0;

            _rawBar.Bipolar = cal.Centred;
            _rawBar.Value = rawNorm;
            _rawBar.RawText = raw.ToString();

            double outv = cal.Apply(raw);
            _calBar.Bipolar = cal.Centred;
            _calBar.Value = outv;
            _calBar.RawText = (cal.Centred ? (outv * 2.0 - 1.0) : outv).ToString("0.000");

            _plot.SetLive(raw, true);
        }

        public void ClearLive()
        {
            _hasLive = false;
            _capturing = false;
            if (_captureButton != null) _captureButton.Text = "Start capture";
            if (_plot != null) _plot.SetLive(0, false);
            UpdateEnabled();
        }

        // =====================================================================
        //  Editing
        // =====================================================================

        private AxisCalibration Selected()
        {
            int i = _axisList.SelectedIndex;
            if (i < 0 || i >= _profile.Axes.Count) return null;
            return _profile.Axes[i];
        }

        private void OnAxisSelected()
        {
            AxisCalibration cal = Selected();
            if (cal == null) { UpdateEnabled(); return; }

            _capturing = false;
            _captureButton.Text = "Start capture";

            _suppressEvents = true;
            _centredBox.Checked = cal.Centred;
            _invertBox.Checked = cal.Invert;
            _dzCentre.Value = Clamp((int)Math.Round(cal.DeadzoneCentre * 100), 0, 45);
            _dzEnds.Value = Clamp((int)Math.Round(cal.DeadzoneEnds * 100), 0, 45);
            _gamma.Value = Clamp((int)Math.Round(cal.Gamma * 100), 50, 250);
            _suppressEvents = false;

            _plot.Calibration = cal;
            UpdateRangeLabel(cal);
            UpdateReadouts();
            UpdateEnabled();
        }

        private void OnFieldChanged()
        {
            if (_suppressEvents) return;
            AxisCalibration cal = Selected();
            if (cal == null) return;

            cal.Centred = _centredBox.Checked;
            cal.Invert = _invertBox.Checked;
            cal.DeadzoneCentre = _dzCentre.Value / 100.0;
            cal.DeadzoneEnds = _dzEnds.Value / 100.0;
            cal.Gamma = _gamma.Value / 100.0;

            UpdateReadouts();
            UpdateEnabled();
            _plot.Invalidate();
        }

        private void UpdateReadouts()
        {
            _dzCentreLabel.Text = _dzCentre.Value + "%";
            _dzEndsLabel.Text = _dzEnds.Value + "%";

            double g = _gamma.Value / 100.0;
            string shape;
            if (Math.Abs(g - 1.0) < 0.02) shape = "linear";
            else if (g > 1.0) shape = "softer";
            else shape = "sharper";
            _gammaLabel.Text = g.ToString("0.00") + "  " + shape;
        }

        private void UpdateRangeLabel(AxisCalibration cal)
        {
            if (cal == null) { _rangeLabel.Text = ""; return; }
            _rangeLabel.Text = "min " + cal.Min + "    max " + cal.Max
                             + "    centre " + cal.Centre
                             + "    span " + (cal.Max - cal.Min);
        }

        private void ToggleCapture()
        {
            AxisCalibration cal = Selected();
            if (cal == null) return;

            _capturing = !_capturing;

            if (_capturing)
            {
                // Collapse the range onto the current position so the sweep
                // grows it outward from where the axis actually is.
                int start = CurrentRaw();
                cal.Min = start;
                cal.Max = start;
                _captureButton.Text = "Stop capture";
            }
            else
            {
                _captureButton.Text = "Start capture";
                if (cal.Centred && (cal.Centre < cal.Min || cal.Centre > cal.Max))
                    cal.Centre = cal.Min + (cal.Max - cal.Min) / 2;
            }

            UpdateRangeLabel(cal);
            UpdateEnabled();
            _plot.Invalidate();
        }

        private int CurrentRaw()
        {
            int i = _axisList.SelectedIndex;
            if (!_hasLive || i < 0 || i >= _liveRaw.Length) return 0;
            return _liveRaw[i];
        }

        private void ResetTravel()
        {
            AxisCalibration cal = Selected();
            if (cal == null || _layout == null) return;

            int i = _axisList.SelectedIndex;
            HidAxis a = _layout.Axes[i];

            _capturing = false;
            _captureButton.Text = "Start capture";
            cal.Min = a.LogicalMin;
            cal.Max = a.LogicalMax;
            cal.Centre = a.LogicalMin + (a.LogicalMax - a.LogicalMin) / 2;

            UpdateRangeLabel(cal);
            UpdateEnabled();
            _plot.Invalidate();
        }

        private void SetCentreHere()
        {
            AxisCalibration cal = Selected();
            if (cal == null || !_hasLive) return;
            cal.Centre = CurrentRaw();
            UpdateRangeLabel(cal);
            _plot.Invalidate();
        }

        private void UpdateEnabled()
        {
            bool haveAxis = Selected() != null;
            bool live = _hasLive && haveAxis;

            if (_emptyLabel != null) _emptyLabel.Visible = !haveAxis;

            Control[] axisControls = new Control[]
            {
                _rawBar, _calBar, _captureButton, _resetButton, _centredBox,
                _invertBox, _dzCentre, _dzEnds, _gamma, _plot, _rangeLabel
            };
            foreach (Control c in axisControls)
                if (c != null) c.Visible = haveAxis;

            if (_captureButton != null) _captureButton.Enabled = live;
            if (_centreButton != null)
            {
                _centreButton.Visible = haveAxis;
                AxisCalibration cal = Selected();
                _centreButton.Enabled = live && cal != null && cal.Centred;
            }
            if (_saveButton != null) _saveButton.Enabled = _profile.Axes.Count > 0;
        }

        private static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

        // =====================================================================
        //  Profiles
        // =====================================================================

        private void RefreshProfileList()
        {
            string previous = _profileBox.SelectedItem as string;
            _profileBox.Items.Clear();

            List<string> names = CalibrationProfile.ListProfiles();
            foreach (string n in names) _profileBox.Items.Add(n);

            if (previous != null && _profileBox.Items.Contains(previous))
                _profileBox.SelectedItem = previous;
            else if (_profileBox.Items.Count > 0)
                _profileBox.SelectedIndex = 0;

            // An empty enabled combo paints as a solid block of Windows
            // highlight blue when it takes focus. Disabling it while there is
            // nothing to pick is both truthful and better looking.
            bool any = _profileBox.Items.Count > 0;
            _profileBox.Enabled = any;
            _loadButton.Enabled = any;
        }

        private void SaveProfile()
        {
            string name = _nameBox.Text.Trim();
            if (name.Length == 0)
            {
                _profileStatus.Text = "Give the profile a name first.";
                _profileStatus.ForeColor = Theme.Warn;
                return;
            }

            _profile.Name = name;

            try
            {
                string path = _profile.Save();
                _profileStatus.Text = "Saved to " + path;
                _profileStatus.ForeColor = Theme.Good;
                RefreshProfileList();
                _profileBox.SelectedItem = name;
            }
            catch (Exception ex)
            {
                _profileStatus.Text = "Could not save: " + ex.Message;
                _profileStatus.ForeColor = Theme.Bad;
            }
        }

        private void LoadSelectedProfile()
        {
            string name = _profileBox.SelectedItem as string;
            if (name == null) return;

            try
            {
                CalibrationProfile loaded = CalibrationProfile.Load(CalibrationProfile.PathFor(name));
                _profile = loaded;
                _nameBox.Text = loaded.Name;

                string note = "Loaded " + loaded.Name;
                if (_device != null && loaded.DeviceVidPid.Length > 0
                    && loaded.DeviceVidPid != _device.VidPid)
                {
                    note += "  —  captured on " + loaded.DeviceVidPid
                          + ", this device is " + _device.VidPid;
                    _profileStatus.ForeColor = Theme.Warn;
                }
                else
                {
                    _profileStatus.ForeColor = Theme.Good;
                }
                _profileStatus.Text = note;

                // Re-merge against the connected device so axes it does not
                // mention still get sensible defaults.
                SetDevice(_device, _layout);
            }
            catch (FileNotFoundException)
            {
                _profileStatus.Text = "That profile file has gone.";
                _profileStatus.ForeColor = Theme.Bad;
                RefreshProfileList();
            }
            catch (Exception ex)
            {
                _profileStatus.Text = "Could not load: " + ex.Message;
                _profileStatus.ForeColor = Theme.Bad;
            }
        }
    }
}
