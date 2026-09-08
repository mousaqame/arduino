using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;
using WheelForge.Core;

namespace WheelForge.Ui
{
    // The pages that are not the live monitor. Everything here is either real
    // information (the board catalogue) or an honest description of what is not
    // built yet -- nothing pretends to work.
    internal static class InfoPages
    {
        private static Panel Scroller()
        {
            Panel p = new Panel();
            p.BackColor = Theme.Window;
            p.AutoScroll = true;
            p.Padding = new Padding(0);
            return p;
        }

        private static Label Body(string text, int width)
        {
            Label l = new Label();
            l.Text = text;
            l.Font = Theme.UiFont;
            l.ForeColor = Theme.TextDim;
            l.BackColor = Color.Transparent;
            l.MaximumSize = new Size(width, 0);
            l.AutoSize = true;
            return l;
        }

        private static Label Note(string text, int width, Color colour)
        {
            Label l = Body(text, width);
            l.ForeColor = colour;
            l.Font = Theme.UiFontSmall;
            return l;
        }

        // =====================================================================

        public static Control BuildBoardsPage()
        {
            Panel page = Scroller();

            // Rows are laid out by hand rather than docked, because their height
            // depends on how far each note wraps, which is only known once the
            // width is known.
            List<Control> rows = new List<Control>();
            List<Label> notes = new List<Label>();

            Card intro = new Card("Board support");
            intro.Padding = new Padding(14, 42, 14, 12);

            Label lead = Body(
                "Whether a board can be a steering wheel on its own comes down to one thing: does it "
              + "have USB device hardware. A chip without it cannot present a game controller no "
              + "matter what firmware it runs -- so those boards stream over serial instead and "
              + "WheelForge feeds a vJoy device on this end. Both routes work; only the first works "
              + "with WheelForge closed.", 700);
            lead.Location = new Point(16, 44);
            intro.Controls.Add(lead);

            Label warn = Note(
                "Every board below has a firmware image built and ready to flash. The wiring under "
              + "each one is what that image expects -- it comes from firmware/boards.h, so it "
              + "cannot drift from the code.", 700, Theme.Warn);
            warn.Location = new Point(16, 100);
            intro.Controls.Add(warn);

            intro.Tag = new Label[] { lead, warn };
            rows.Add(intro);
            page.Controls.Add(intro);

            List<Label> wirings = new List<Label>();

            foreach (BoardProfile b in BoardCatalog.All)
            {
                Card row = new Card(null);
                row.Padding = new Padding(14, 10, 14, 10);

                Color verdictColour;
                switch (b.Mode)
                {
                    case LinkMode.Hid: verdictColour = Theme.Good; break;
                    case LinkMode.Bridge: verdictColour = Theme.Warn; break;
                    default: verdictColour = Theme.Bad; break;
                }

                row.Controls.Add(Theme.MakeLabel(b.Name, Theme.UiFontBold, Theme.Text, 16, 12));
                row.Controls.Add(Theme.MakeLabel(b.Mcu, Theme.MonoFontSmall, Theme.TextFaint, 18, 32));
                row.Controls.Add(Theme.MakeLabel(b.Verdict, Theme.UiFontBold, verdictColour, 300, 12));
                row.Controls.Add(Theme.MakeLabel(b.Caveat, Theme.UiFontSmall, Theme.TextFaint, 300, 31));

                Label note = Note(b.Note, 460, Theme.TextDim);
                note.Location = new Point(16, 56);
                row.Controls.Add(note);

                Label wiring = new Label();
                wiring.Text = string.Join("\r\n", b.Wiring);
                wiring.Font = Theme.MonoFontSmall;
                wiring.ForeColor = Theme.Cool;
                wiring.BackColor = Color.Transparent;
                wiring.AutoSize = true;
                row.Controls.Add(wiring);

                notes.Add(note);
                wirings.Add(wiring);
                rows.Add(row);
                page.Controls.Add(row);
            }

            EventHandler reflow = delegate
            {
                int width = page.ClientSize.Width - 4;
                if (page.VerticalScroll.Visible) width -= SystemInformation.VerticalScrollBarWidth;
                if (width < 420) width = 420;

                int y = 0;
                foreach (Control row in rows)
                {
                    row.Width = width;

                    int height;
                    Label[] pair = row.Tag as Label[];
                    if (pair != null)
                    {
                        foreach (Label l in pair) l.MaximumSize = new Size(width - 36, 0);
                        pair[1].Top = pair[0].Bottom + 14;
                        height = pair[1].Bottom + 14;
                    }
                    else
                    {
                        int i = rows.IndexOf(row) - 1;
                        Label note = notes[i];
                        Label wiring = wirings[i];
                        note.MaximumSize = new Size(width - 40, 0);
                        wiring.Top = note.Bottom + 10;
                        wiring.Left = 20;
                        height = Math.Max(72, wiring.Bottom + 12);
                    }

                    row.Height = height;
                    row.Location = new Point(0, y);
                    y += height + 10;
                }
            };

            page.Resize += reflow;
            page.HandleCreated += reflow;
            reflow(null, EventArgs.Empty);

            return page;
        }

        // =====================================================================

        public static Control BuildModesPage()
        {
            Panel page = Scroller();

            Card card = new Card("How the wheel presents itself to games");
            card.Dock = DockStyle.Top;
            card.Height = 520;

            int y = 46;

            Label lead = Body(
                "A game only feels force feedback if it recognises the device. There are three ways "
              + "to be recognised, and they trade off differently.", 800);
            lead.Location = new Point(16, y);
            card.Controls.Add(lead);
            y += 46;

            string[][] modes = new string[][]
            {
                new string[]
                {
                    "Generic HID force feedback  (what the firmware does now)",
                    "The firmware declares a full PID interface -- constant, ramp, square, sine, "
                  + "triangle, sawtooth, spring, damper, inertia and friction -- which is exactly "
                  + "what DirectInput drives. Works in Assetto Corsa, ACC, iRacing, rFactor 2, "
                  + "Automobilista 2, Dirt Rally, RBR, BeamNG and Euro Truck. This is the honest, "
                  + "correct option and the one to use.",
                    "good"
                },
                new string[]
                {
                    "Xbox 360 pad  (via ViGEmBus)",
                    "For games that will only talk to a gamepad. The wheel becomes an X360 controller "
                  + "with steering on the left stick. Broad compatibility, but no force feedback "
                  + "beyond rumble, and the steering resolution drops to pad range. Not built.",
                    "warn"
                },
                new string[]
                {
                    "Logitech wheel identity",
                    "Report the USB identity and report format of a G29 or G27, and speak Logitech's "
                  + "own force feedback command set. The only thing that satisfies games with a "
                  + "hardcoded list of supported wheels -- which is exactly why the vJoy and EMUWheel "
                  + "layer exists on this rig today. Not built.",
                    "warn"
                }
            };

            foreach (string[] m in modes)
            {
                RadioButton rb = new RadioButton();
                rb.Text = m[0];
                rb.Font = Theme.UiFontBold;
                rb.ForeColor = Theme.Text;
                rb.BackColor = Color.Transparent;
                rb.Location = new Point(16, y);
                rb.AutoSize = true;
                rb.Enabled = false;
                rb.Checked = (m[2] == "good");
                card.Controls.Add(rb);
                y += 24;

                Label d = Body(m[1], 800);
                d.Location = new Point(36, y);
                card.Controls.Add(d);
                y += d.PreferredHeight + 18;
            }

            Label bridgeNote = Note(
                "Bridge boards -- Uno, Mega, Nano, classic ESP32, ESP8266 -- cannot do any of this. "
              + "Force feedback needs the game to send reports to the device, and a board with no USB "
              + "hardware has nothing to receive them. They still drive the motor for the Wheel test, "
              + "because that comes from WheelForge over the serial link instead.",
                800, Theme.TextDim);
            bridgeNote.Location = new Point(16, y + 4);
            card.Controls.Add(bridgeNote);
            y += bridgeNote.PreferredHeight + 22;

            Label caveat = Note(
                "On the Logitech option: that USB vendor id belongs to Logitech, so it would be a "
              + "switch you turn on for your own wheel rather than something shipped pre-flashed. "
              + "And G HUB will try to claim any device that answers to it.",
                800, Theme.TextFaint);
            caveat.Location = new Point(16, y);
            card.Controls.Add(caveat);

            card.Height = y + caveat.PreferredHeight + 30;

            page.Controls.Add(card);
            return page;
        }

        // =====================================================================

        // =====================================================================

        public static Control BuildAboutPage()
        {
            Panel page = Scroller();

            Card card = new Card("WheelForge");
            card.Dock = DockStyle.Top;
            card.Height = 400;

            int y = 46;

            Label lead = Body(
                "A configuration and calibration tool for DIY force feedback wheels, in the same shape "
              + "as the EMC utility but open, and aimed at more than one board.",
                800);
            lead.Location = new Point(16, y);
            card.Controls.Add(lead);
            y += 52;

            Label h1 = Theme.MakeLabel("It does not write to a running wheel", Theme.HeadingFont, Theme.Text, 16, y);
            card.Controls.Add(h1);
            y += 26;

            Label p1 = Body(
                "There is no code path in this build that writes to a HID device. The wheel on this "
              + "desk runs EMC Lite, and a stray write is the one thing that could disturb it mid-race. "
              + "Monitoring and calibration are pure reads.\r\n\r\n"
              + "Flashing is the one exception, and it is deliberately not subtle: it goes through "
              + "avrdude over the serial bootloader, never over HID, and it asks first and says what "
              + "it is about to erase.",
                800);
            p1.Location = new Point(16, y);
            card.Controls.Add(p1);
            y += p1.PreferredHeight + 24;

            Label h2 = Theme.MakeLabel("Reading is shared", Theme.HeadingFont, Theme.Text, 16, y);
            card.Controls.Add(h2);
            y += 26;

            Label p2 = Body(
                "The wheel is opened with both share flags, so a game can hold it at the same time. "
              + "You can leave this open on a second monitor while you drive. Device listing opens "
              + "with access 0 — identity only — which Windows answers even for devices another "
              + "program owns exclusively.",
                800);
            p2.Location = new Point(16, y);
            card.Controls.Add(p2);
            y += p2.PreferredHeight + 24;

            Label h3 = Theme.MakeLabel("How it reads any wheel", Theme.HeadingFont, Theme.Text, 16, y);
            card.Controls.Add(h3);
            y += 26;

            Label p3 = Body(
                "Axes and buttons are discovered from the device's own report descriptor through the "
              + "HidP parsing API, not from hardcoded byte offsets. That is why it works against EMC "
              + "Lite, a Logitech wheel, vJoy or our own firmware without being told the layout first.",
                800);
            p3.Location = new Point(16, y);
            card.Controls.Add(p3);
            y += p3.PreferredHeight + 24;

            Label p4 = Note(
                "Built for .NET Framework 4.x, which ships with Windows — the executable runs on any "
              + "Windows machine with nothing installed.",
                800, Theme.TextFaint);
            p4.Location = new Point(16, y);
            card.Controls.Add(p4);

            card.Height = y + 60;

            page.Controls.Add(card);
            return page;
        }
    }
}
