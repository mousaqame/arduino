using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;
using WheelForge.Core;

namespace WheelForge.Ui
{
    // Wiring and flashing, for whichever board you actually have.
    //
    // The pin tables come from BoardCatalog, which mirrors firmware/boards.h,
    // so what this page tells you to wire is what the firmware really reads.
    public class SetupGuidePage : Panel
    {
        private ComboBox _boardBox;
        private Panel _body;
        private readonly List<Control> _blocks = new List<Control>();

        public SetupGuidePage()
        {
            BackColor = Theme.Window;
            Build();
            if (_boardBox.Items.Count > 0) _boardBox.SelectedIndex = 0;
        }

        private void Build()
        {
            Card picker = new Card(null);
            picker.Dock = DockStyle.Top;
            picker.Height = 60;
            picker.Padding = new Padding(14, 10, 14, 10);

            picker.Controls.Add(Theme.MakeLabel("Board", Theme.UiFont, Theme.TextDim, 4, 10));

            _boardBox = new ComboBox();
            _boardBox.DropDownStyle = ComboBoxStyle.DropDownList;
            _boardBox.SetBounds(56, 6, 320, 24);
            _boardBox.BackColor = Theme.PanelHi;
            _boardBox.ForeColor = Theme.Text;
            _boardBox.FlatStyle = FlatStyle.Flat;
            foreach (BoardProfile b in BoardCatalog.All) _boardBox.Items.Add(b.Name);
            _boardBox.SelectedIndexChanged += delegate { Rebuild(); };
            picker.Controls.Add(_boardBox);

            _body = new Panel();
            _body.Dock = DockStyle.Fill;
            _body.BackColor = Theme.Window;
            _body.AutoScroll = true;
            _body.Resize += delegate { Reflow(); };

            Controls.Add(_body);
            Controls.Add(picker);
        }

        private BoardProfile Selected()
        {
            int i = _boardBox.SelectedIndex;
            List<BoardProfile> all = BoardCatalog.All;
            if (i < 0 || i >= all.Count) return null;
            return all[i];
        }

        // ------------------------------------------------------------------

        private Card Section(string title, string intro, string[] mono, string[] bullets)
        {
            Card card = new Card(title);
            card.Padding = new Padding(14, 42, 14, 12);

            List<Control> flow = new List<Control>();

            if (!string.IsNullOrEmpty(intro))
            {
                Label l = new Label();
                l.Text = intro;
                l.Font = Theme.UiFont;
                l.ForeColor = Theme.TextDim;
                l.BackColor = Color.Transparent;
                l.AutoSize = true;
                card.Controls.Add(l);
                flow.Add(l);
            }

            if (bullets != null)
            {
                foreach (string b in bullets)
                {
                    Label l = new Label();
                    l.Text = b;
                    l.Font = Theme.UiFont;
                    l.ForeColor = Theme.Text;
                    l.BackColor = Color.Transparent;
                    l.AutoSize = true;
                    card.Controls.Add(l);
                    flow.Add(l);
                }
            }

            if (mono != null && mono.Length > 0)
            {
                Label l = new Label();
                l.Text = string.Join("\r\n", mono);
                l.Font = Theme.MonoFontSmall;
                l.ForeColor = Theme.Cool;
                l.BackColor = Color.Transparent;
                l.AutoSize = true;
                card.Controls.Add(l);
                flow.Add(l);
            }

            card.Tag = flow;
            return card;
        }

        private void Rebuild()
        {
            foreach (Control c in _blocks) _body.Controls.Remove(c);
            _blocks.Clear();

            BoardProfile b = Selected();
            if (b == null) return;

            // ---- 1. what this board can be
            string what;
            if (b.Mode == LinkMode.Hid)
                what = b.Name + " has USB device hardware, so once it is flashed Windows sees a "
                     + "real game controller. It works with WheelForge closed, and games can send "
                     + "it force feedback.";
            else
                what = b.Name + " has no USB device hardware. Windows only ever sees a serial port, "
                     + "so it streams to WheelForge and WheelForge feeds a vJoy controller. Everything "
                     + "works except force feedback from a game, and WheelForge has to stay running.";

            _blocks.Add(Section("What you get", what, null, null));

            // ---- 2. wiring, the general part
            _blocks.Add(Section("Wiring — the three things people get wrong",
                null, null, new string[]
            {
                "PEDALS.  Each pedal is a potentiometer with three legs. The two outer legs go to "
              + "5 V and GND (3.3 V on a Pico or ESP — never 5 V). The middle leg, the wiper, is "
              + "the signal and goes to the analogue pin. Swapping the outer two just reverses the "
              + "pedal, which the Calibration page can undo; putting 5 V on the wiper of a 3.3 V "
              + "board damages it.",

                "BUTTONS.  They are a matrix, not switches to ground. Each button bridges one "
              + "column pin and one row pin. A button wired to GND will never register. Once two "
              + "buttons can be held at once you need a 1N4148 diode in series with each, band "
              + "towards the row pin, or simultaneous presses show up as buttons you did not press.",

                "ENCODER.  A and B go to the two pins listed below, which are chosen because they "
              + "are interrupt capable. Its supply is 5 V or 3.3 V to match the board. If the wheel "
              + "reads backwards, swap A and B, or tick Invert on the Wheel test page.",

                "MOTOR.  The driver takes its own power supply — never from the board's USB. Its "
              + "grounds and the board's must be joined, or nothing works and the readings wander."
            }));

            // ---- 3. this board's pins
            _blocks.Add(Section("Wiring — " + b.Name,
                "These are the pins the shipped firmware image actually reads.",
                b.Wiring, null));

            // ---- 4. flashing mode
            if (b.BootMode != null && b.BootMode.Length > 0)
            {
                _blocks.Add(Section("Putting it in flashing mode", null, b.BootMode, null));
            }

            // ---- 5. the flash itself
            List<string> steps = new List<string>();
            steps.Add("1.  Plug the board in on its own. Close the Arduino IDE's serial monitor "
                    + "and anything else using the port.");
            if (b.Tool == FlashTool.Uf2Copy)
                steps.Add("2.  Firmware page: pick " + b.Name + ". There is no port to choose.");
            else
                steps.Add("2.  Firmware page: pick " + b.Name + ", then its COM port. Press Refresh "
                        + "if it is not listed.");
            steps.Add("3.  Check the Image box points at " + (b.HasImage ? b.ImageName : "an image")
                    + ". It fills in by itself when the image is where WheelForge expects.");
            steps.Add("4.  Press Flash and confirm. Watch the Output panel — it shows exactly what "
                    + "the flashing tool says.");

            if (b.Mode == LinkMode.Hid)
                steps.Add("5.  The board disappears and comes back as a game controller. It then "
                        + "shows up in the device list at the top, and in Windows' own joy.cpl.");
            else
                steps.Add("5.  Wheel test page: connect to the same COM port, then tick Feed vJoy. "
                        + "The wheel now appears to games as a vJoy controller.");

            _blocks.Add(Section("Flashing it", null, null, steps.ToArray()));

            // ---- 6. after
            List<string> after = new List<string>();
            after.Add("Wheel test page: set your encoder's PPR and the rotation you want, press "
                    + "Set centre with the wheel straight, then Save to board.");
            after.Add("Calibration page: release the pedals and press Set Min, floor them and press "
                    + "Set Max.");
            if (b.SupportsFfb)
                after.Add("Wheel test page: arm the motor and try the centring spring at low "
                        + "strength. If it fights you instead of centring, tick Invert force "
                        + "direction.");
            _blocks.Add(Section("Once it is flashed", null, null, after.ToArray()));

            foreach (Control c in _blocks) _body.Controls.Add(c);
            Reflow();
        }

        // Heights depend on how far the text wraps, which is only known once the
        // width is.
        private void Reflow()
        {
            int width = _body.ClientSize.Width - 4;
            if (_body.VerticalScroll.Visible) width -= SystemInformation.VerticalScrollBarWidth;
            if (width < 420) width = 420;

            int y = 0;
            foreach (Control card in _blocks)
            {
                card.Width = width;

                List<Control> flow = card.Tag as List<Control>;
                int inner = 42;

                if (flow != null)
                {
                    foreach (Control c in flow)
                    {
                        Label l = c as Label;
                        if (l != null && l.Font != Theme.MonoFontSmall)
                            l.MaximumSize = new Size(width - 40, 0);

                        c.Location = new Point(18, inner);
                        inner = c.Bottom + 12;
                    }
                }

                card.Height = inner + 6;
                card.Location = new Point(0, y);
                y += card.Height + 10;
            }
        }
    }
}
