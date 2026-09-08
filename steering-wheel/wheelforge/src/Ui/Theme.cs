using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace WheelForge.Ui
{
    internal static class Theme
    {
        [DllImport("dwmapi.dll")]
        private static extern int DwmSetWindowAttribute(
            IntPtr hwnd, int attribute, ref int value, int size);

        // Windows will not theme a title bar to match the window on its own. 20
        // is the attribute on current Windows 10 and 11; 19 was used on the
        // 1809 builds. Both are tried and both are allowed to fail -- an
        // untouched title bar is cosmetic, not a problem worth an error.
        public static void ApplyDarkTitleBar(IntPtr handle)
        {
            if (handle == IntPtr.Zero) return;
            int on = 1;
            try
            {
                if (DwmSetWindowAttribute(handle, 20, ref on, sizeof(int)) != 0)
                    DwmSetWindowAttribute(handle, 19, ref on, sizeof(int));
            }
            catch (DllNotFoundException)
            {
                // Pre-Vista. Nothing to do.
            }
            catch (EntryPointNotFoundException)
            {
            }
        }

        public static readonly Color Window = Color.FromArgb(0x14, 0x17, 0x1C);
        public static readonly Color Rail = Color.FromArgb(0x11, 0x13, 0x18);
        public static readonly Color Panel = Color.FromArgb(0x1C, 0x20, 0x27);
        public static readonly Color PanelHi = Color.FromArgb(0x23, 0x28, 0x31);
        public static readonly Color Line = Color.FromArgb(0x2E, 0x34, 0x3F);

        public static readonly Color Text = Color.FromArgb(0xE6, 0xE8, 0xEB);
        public static readonly Color TextDim = Color.FromArgb(0x8A, 0x91, 0x9C);
        public static readonly Color TextFaint = Color.FromArgb(0x5C, 0x63, 0x6E);

        public static readonly Color Accent = Color.FromArgb(0xE8, 0xA3, 0x3D);
        public static readonly Color AccentDim = Color.FromArgb(0x7A, 0x56, 0x21);
        public static readonly Color Good = Color.FromArgb(0x4C, 0xC3, 0x8A);
        public static readonly Color Warn = Color.FromArgb(0xE0, 0xB3, 0x4F);
        public static readonly Color Bad = Color.FromArgb(0xE5, 0x53, 0x4B);
        public static readonly Color Cool = Color.FromArgb(0x5A, 0xB0, 0xE8);

        public static readonly Font UiFont = new Font("Segoe UI", 9f);
        public static readonly Font UiFontBold = new Font("Segoe UI", 9f, FontStyle.Bold);
        public static readonly Font UiFontSmall = new Font("Segoe UI", 8f);
        public static readonly Font TitleFont = new Font("Segoe UI Semibold", 13f);
        public static readonly Font HeadingFont = new Font("Segoe UI Semibold", 10.5f);
        public static readonly Font MonoFont = new Font("Consolas", 9f);
        public static readonly Font MonoFontSmall = new Font("Consolas", 8f);

        // Walks a control tree applying the dark palette. WinForms has no theme
        // engine, so this is done once after each page is built.
        public static void Apply(Control root)
        {
            foreach (Control c in root.Controls)
            {
                if (c is Button)
                {
                    StyleButton((Button)c);
                }
                else if (c is TextBox)
                {
                    TextBox t = (TextBox)c;
                    t.BackColor = PanelHi;
                    t.ForeColor = Text;
                    t.BorderStyle = BorderStyle.FixedSingle;
                }
                else if (c is ComboBox)
                {
                    ComboBox cb = (ComboBox)c;
                    cb.BackColor = PanelHi;
                    cb.ForeColor = Text;
                    cb.FlatStyle = FlatStyle.Flat;
                }
                else if (c is ListBox)
                {
                    ListBox lb = (ListBox)c;
                    lb.BackColor = Panel;
                    lb.ForeColor = Text;
                    lb.BorderStyle = BorderStyle.None;
                }
                else if (c is CheckBox || c is RadioButton || c is Label)
                {
                    c.ForeColor = Text;
                    c.BackColor = Color.Transparent;
                }

                if (c.Controls.Count > 0) Apply(c);
            }
        }

        public static void StyleButton(Button b)
        {
            b.FlatStyle = FlatStyle.Flat;
            b.BackColor = PanelHi;
            b.ForeColor = Text;
            b.FlatAppearance.BorderColor = Line;
            b.FlatAppearance.BorderSize = 1;
            b.FlatAppearance.MouseOverBackColor = Color.FromArgb(0x2C, 0x32, 0x3D);
            b.FlatAppearance.MouseDownBackColor = Color.FromArgb(0x36, 0x3D, 0x4A);
            b.Font = UiFont;
            b.Cursor = Cursors.Hand;
        }

        public static void StylePrimary(Button b)
        {
            StyleButton(b);
            b.BackColor = AccentDim;
            b.ForeColor = Color.FromArgb(0xFF, 0xEC, 0xD2);
            b.FlatAppearance.BorderColor = Accent;
            b.FlatAppearance.MouseOverBackColor = Color.FromArgb(0x93, 0x68, 0x28);
            b.FlatAppearance.MouseDownBackColor = Color.FromArgb(0x6A, 0x4A, 0x1C);
        }

        public static Label MakeLabel(string text, Font font, Color colour, int x, int y)
        {
            Label l = new Label();
            l.Text = text;
            l.Font = font;
            l.ForeColor = colour;
            l.BackColor = Color.Transparent;
            l.Location = new Point(x, y);
            l.AutoSize = true;
            return l;
        }
    }
}
