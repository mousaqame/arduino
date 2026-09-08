using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace WheelForge.Ui
{
    // A titled card. Everything on a page sits in one of these.
    public class Card : Panel
    {
        private string _title;

        public Card(string title)
        {
            _title = title;
            BackColor = Theme.Panel;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                     | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
            Padding = new Padding(14, string.IsNullOrEmpty(title) ? 12 : 38, 14, 14);
        }

        public string Title
        {
            get { return _title; }
            set { _title = value; Invalidate(); }
        }

        // Docked children lay out inside DisplayRectangle, so the title strip has
        // to be carved out here. Relying on Padding alone is not enough: a
        // Dock.Fill child with its own background paints straight over the
        // heading.
        public override Rectangle DisplayRectangle
        {
            get
            {
                int top = string.IsNullOrEmpty(_title) ? Padding.Top : TitleHeight;
                int w = Width - Padding.Left - Padding.Right;
                int h = Height - top - Padding.Bottom;
                return new Rectangle(Padding.Left, top, w < 0 ? 0 : w, h < 0 ? 0 : h);
            }
        }

        private const int TitleHeight = 42;

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(Theme.Panel);

            using (Pen p = new Pen(Theme.Line))
                g.DrawRectangle(p, 0, 0, Width - 1, Height - 1);

            if (!string.IsNullOrEmpty(_title))
            {
                using (Brush b = new SolidBrush(Theme.Text))
                    g.DrawString(_title, Theme.HeadingFont, b, 13, 11);

                using (Pen p = new Pen(Theme.Line))
                    g.DrawLine(p, 13, 33, Width - 14, 33);
            }

            base.OnPaint(e);
        }
    }

    // One analogue axis: caption, a bar, and the raw number.
    // Bipolar axes (steering) fill outward from the centre; unipolar axes
    // (pedals) fill from the left.
    public class AxisBar : Control
    {
        private double _value;          // 0..1
        private string _caption = "";
        private string _rawText = "";
        private bool _bipolar;
        private Color _colour;

        public AxisBar()
        {
            _colour = Theme.Accent;
            Height = 26;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                     | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
        }

        public string Caption
        {
            get { return _caption; }
            set { _caption = value; Invalidate(); }
        }

        public string RawText
        {
            get { return _rawText; }
            set { if (_rawText != value) { _rawText = value; Invalidate(); } }
        }

        public bool Bipolar
        {
            get { return _bipolar; }
            set { _bipolar = value; Invalidate(); }
        }

        public Color BarColour
        {
            get { return _colour; }
            set { _colour = value; Invalidate(); }
        }

        public double Value
        {
            get { return _value; }
            set
            {
                double v = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
                if (Math.Abs(v - _value) < 0.0005) return;
                _value = v;
                Invalidate();
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(BackColor);

            const int captionWidth = 104;
            const int valueWidth = 86;

            using (Brush b = new SolidBrush(Theme.TextDim))
            {
                StringFormat sf = new StringFormat();
                sf.LineAlignment = StringAlignment.Center;
                sf.Trimming = StringTrimming.EllipsisCharacter;
                sf.FormatFlags = StringFormatFlags.NoWrap;
                g.DrawString(_caption, Theme.UiFont, b,
                    new RectangleF(0, 0, captionWidth - 8, Height), sf);
            }

            int trackX = captionWidth;
            int trackW = Width - captionWidth - valueWidth;
            if (trackW < 20) return;

            int trackY = Height / 2 - 5;
            Rectangle track = new Rectangle(trackX, trackY, trackW, 10);

            using (Brush b = new SolidBrush(Theme.Window))
                g.FillRectangle(b, track);
            using (Pen p = new Pen(Theme.Line))
                g.DrawRectangle(p, track);

            if (_bipolar)
            {
                int mid = trackX + trackW / 2;
                double offset = _value - 0.5;              // -0.5 .. +0.5
                int fill = (int)Math.Round(Math.Abs(offset) * trackW);

                if (fill > 0)
                {
                    Rectangle bar = offset >= 0
                        ? new Rectangle(mid, trackY + 1, fill, 8)
                        : new Rectangle(mid - fill, trackY + 1, fill, 8);
                    using (Brush b = new SolidBrush(_colour))
                        g.FillRectangle(b, bar);
                }

                using (Pen p = new Pen(Theme.TextFaint))
                    g.DrawLine(p, mid, trackY - 3, mid, trackY + 12);
            }
            else
            {
                int fill = (int)Math.Round(_value * (trackW - 2));
                if (fill > 0)
                {
                    using (Brush b = new SolidBrush(_colour))
                        g.FillRectangle(b, new Rectangle(trackX + 1, trackY + 1, fill, 8));
                }
            }

            using (Brush b = new SolidBrush(Theme.Text))
            {
                StringFormat sf = new StringFormat();
                sf.LineAlignment = StringAlignment.Center;
                sf.Alignment = StringAlignment.Far;
                g.DrawString(_rawText, Theme.MonoFontSmall, b,
                    new RectangleF(Width - valueWidth, 0, valueWidth - 2, Height), sf);
            }
        }
    }

    // A circular gauge for the steering axis. Shows the wheel angle the way a
    // driver thinks about it rather than as a raw number.
    public class SteeringDial : Control
    {
        private double _value = 0.5;    // 0..1, 0.5 centred
        private int _rangeDegrees = 900;

        public SteeringDial()
        {
            Size = new Size(190, 190);
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                     | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
        }

        public int RangeDegrees
        {
            get { return _rangeDegrees; }
            set { _rangeDegrees = value < 90 ? 90 : value; Invalidate(); }
        }

        public double Value
        {
            get { return _value; }
            set
            {
                double v = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
                if (Math.Abs(v - _value) < 0.0005) return;
                _value = v;
                Invalidate();
            }
        }

        public double Degrees
        {
            get { return (_value - 0.5) * _rangeDegrees; }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(BackColor);

            int size = Math.Min(Width, Height) - 16;
            if (size < 40) return;

            Rectangle box = new Rectangle((Width - size) / 2, (Height - size) / 2, size, size);

            using (Pen p = new Pen(Theme.Line, 10))
                g.DrawArc(p, box, 135, 270);

            // Filled sweep from top centre out to the current angle.
            double frac = _value - 0.5;                 // -0.5 .. +0.5
            float sweep = (float)(frac * 270.0);
            if (Math.Abs(sweep) > 0.4f)
            {
                using (Pen p = new Pen(Theme.Accent, 10))
                {
                    p.StartCap = LineCap.Round;
                    p.EndCap = LineCap.Round;
                    g.DrawArc(p, box, 270, sweep);
                }
            }

            using (Pen p = new Pen(Theme.TextFaint, 2))
                g.DrawLine(p, Width / 2, box.Top - 5, Width / 2, box.Top + 4);

            // Needle
            double angle = (270 + sweep) * Math.PI / 180.0;
            int cx = box.Left + size / 2;
            int cy = box.Top + size / 2;
            int r = size / 2 - 14;
            using (Pen p = new Pen(Theme.Text, 2))
            {
                p.EndCap = LineCap.Round;
                g.DrawLine(p, cx, cy,
                    (float)(cx + Math.Cos(angle) * r),
                    (float)(cy + Math.Sin(angle) * r));
            }
            using (Brush b = new SolidBrush(Theme.Accent))
                g.FillEllipse(b, cx - 4, cy - 4, 8, 8);

            StringFormat sf = new StringFormat();
            sf.Alignment = StringAlignment.Center;

            string text = Degrees.ToString("0") + "°";
            using (Brush b = new SolidBrush(Theme.Text))
                g.DrawString(text, Theme.TitleFont, b, new RectangleF(0, cy + 14, Width, 26), sf);

            using (Brush b = new SolidBrush(Theme.TextFaint))
                g.DrawString(_rangeDegrees + "° range", Theme.UiFontSmall, b,
                    new RectangleF(0, cy + 40, Width, 20), sf);
        }
    }

    // A lit cell per button, laid out in rows. Sized from the device's own
    // declared button count.
    public class ButtonGrid : Control
    {
        private int _count;
        private string _emptyText = "No device connected.";
        private readonly HashSet<int> _pressed = new HashSet<int>();
        private readonly HashSet<int> _everPressed = new HashSet<int>();

        private const int CellW = 34;
        private const int CellH = 24;
        private const int Gap = 4;

        public ButtonGrid()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                     | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
        }

        public int ButtonCount
        {
            get { return _count; }
            set
            {
                _count = value < 0 ? 0 : value;
                _pressed.Clear();
                _everPressed.Clear();
                FitHeight();
                Invalidate();
            }
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            FitHeight();
        }

        // When docked to the top of a scrolling host, grow to whatever the rows
        // need. A 128 button device otherwise loses its last row off the bottom.
        private void FitHeight()
        {
            if (Dock != DockStyle.Top) return;
            int needed = Math.Max(1, RowsNeeded) * (CellH + Gap) + 2;
            if (Height != needed) Height = needed;
        }

        // Shown when there is nothing to draw. Says why, rather than always
        // blaming the device.
        public string EmptyText
        {
            get { return _emptyText; }
            set { _emptyText = value; Invalidate(); }
        }

        // Buttons seen pressed at least once since connecting. Makes "which cell
        // is this switch" obvious when mapping a new wheel.
        public void ClearHistory()
        {
            _everPressed.Clear();
            Invalidate();
        }

        public void SetPressed(List<int> zeroBased)
        {
            bool changed = false;

            foreach (int b in zeroBased)
            {
                if (_pressed.Add(b)) changed = true;
                _everPressed.Add(b);
            }

            List<int> gone = new List<int>();
            foreach (int b in _pressed)
                if (!zeroBased.Contains(b)) gone.Add(b);

            foreach (int b in gone)
            {
                _pressed.Remove(b);
                changed = true;
            }

            if (changed) Invalidate();
        }

        public int RowsNeeded
        {
            get
            {
                int perRow = PerRow;
                if (perRow < 1 || _count < 1) return 1;
                return (_count + perRow - 1) / perRow;
            }
        }

        private int PerRow
        {
            get
            {
                int usable = Width - 2;
                int n = usable / (CellW + Gap);
                return n < 1 ? 1 : n;
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(BackColor);

            if (_count == 0)
            {
                using (Brush b = new SolidBrush(Theme.TextFaint))
                    g.DrawString(_emptyText, Theme.UiFont, b, 2, 4);
                return;
            }

            int perRow = PerRow;
            StringFormat sf = new StringFormat();
            sf.Alignment = StringAlignment.Center;
            sf.LineAlignment = StringAlignment.Center;

            for (int i = 0; i < _count; i++)
            {
                int col = i % perRow;
                int row = i / perRow;
                Rectangle cell = new Rectangle(col * (CellW + Gap), row * (CellH + Gap), CellW, CellH);

                bool down = _pressed.Contains(i);
                bool seen = _everPressed.Contains(i);

                Color fill = down ? Theme.Accent : (seen ? Theme.PanelHi : Theme.Window);
                Color edge = down ? Theme.Accent : (seen ? Theme.AccentDim : Theme.Line);
                Color ink = down ? Color.FromArgb(0x1A, 0x14, 0x08)
                                 : (seen ? Theme.Text : Theme.TextFaint);

                using (Brush b = new SolidBrush(fill))
                    g.FillRectangle(b, cell);
                using (Pen p = new Pen(edge))
                    g.DrawRectangle(p, cell);
                using (Brush b = new SolidBrush(ink))
                    g.DrawString((i + 1).ToString(), Theme.UiFontSmall, b, cell, sf);
            }
        }
    }

    // A slider that matches the rest of the window.
    //
    // WinForms TrackBar is drawn by the common controls library and ignores
    // BackColor entirely, so on a dark page it stays Windows blue on grey. This
    // is the same control drawn by hand.
    public class Slider : Control
    {
        private int _min, _max = 100, _value;
        private bool _dragging;

        public event EventHandler ValueChanged;

        public Slider()
        {
            Height = 26;
            Cursor = Cursors.Hand;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                     | ControlStyles.UserPaint | ControlStyles.ResizeRedraw | ControlStyles.Selectable, true);
        }

        public int Minimum
        {
            get { return _min; }
            set { _min = value; if (_value < _min) Value = _min; Invalidate(); }
        }

        public int Maximum
        {
            get { return _max; }
            set { _max = value; if (_value > _max) Value = _max; Invalidate(); }
        }

        public int Value
        {
            get { return _value; }
            set
            {
                int v = value < _min ? _min : (value > _max ? _max : value);
                if (v == _value) return;
                _value = v;
                Invalidate();
                EventHandler h = ValueChanged;
                if (h != null) h(this, EventArgs.Empty);
            }
        }

        private Rectangle Track
        {
            get { return new Rectangle(8, Height / 2 - 2, Math.Max(1, Width - 16), 4); }
        }

        private int ThumbX
        {
            get
            {
                Rectangle t = Track;
                double f = _max > _min ? (_value - (double)_min) / (_max - _min) : 0.0;
                return t.Left + (int)Math.Round(f * t.Width);
            }
        }

        private void SetFromMouse(int x)
        {
            Rectangle t = Track;
            if (t.Width <= 0) return;
            double f = (x - (double)t.Left) / t.Width;
            if (f < 0) f = 0;
            if (f > 1) f = 1;
            Value = _min + (int)Math.Round(f * (_max - _min));
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            base.OnMouseDown(e);
            if (e.Button != MouseButtons.Left) return;
            _dragging = true;
            Focus();
            SetFromMouse(e.X);
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            base.OnMouseMove(e);
            if (_dragging) SetFromMouse(e.X);
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            base.OnMouseUp(e);
            _dragging = false;
        }

        protected override bool IsInputKey(Keys key)
        {
            if (key == Keys.Left || key == Keys.Right) return true;
            return base.IsInputKey(key);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            base.OnKeyDown(e);
            if (e.KeyCode == Keys.Left) Value = _value - 1;
            else if (e.KeyCode == Keys.Right) Value = _value + 1;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(BackColor);

            Rectangle t = Track;
            using (Brush b = new SolidBrush(Theme.Window))
                g.FillRectangle(b, t);
            using (Pen p = new Pen(Theme.Line))
                g.DrawRectangle(p, t);

            int x = ThumbX;
            if (x > t.Left)
            {
                using (Brush b = new SolidBrush(Theme.AccentDim))
                    g.FillRectangle(b, new Rectangle(t.Left, t.Top, x - t.Left, t.Height));
            }

            using (Brush b = new SolidBrush(Theme.Accent))
                g.FillEllipse(b, x - 7, Height / 2 - 7, 14, 14);
            using (Pen p = new Pen(Theme.Window, 2f))
                g.DrawEllipse(p, x - 7, Height / 2 - 7, 14, 14);
        }
    }

    // Plots what the calibration does to an axis: raw travel along the bottom,
    // calibrated output up the side, with a live dot showing where the axis is
    // sitting right now. Deadzones show up as flat runs, the curve as a bend.
    public class CurvePlot : Control
    {
        private WheelForge.Core.AxisCalibration _cal;
        private int _liveRaw;
        private bool _hasLive;

        public CurvePlot()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                     | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
        }

        public WheelForge.Core.AxisCalibration Calibration
        {
            get { return _cal; }
            set { _cal = value; Invalidate(); }
        }

        public void SetLive(int raw, bool has)
        {
            if (_hasLive == has && _liveRaw == raw) return;
            _liveRaw = raw;
            _hasLive = has;
            Invalidate();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(BackColor);

            Rectangle box = new Rectangle(1, 1, Width - 3, Height - 3);
            if (box.Width < 20 || box.Height < 20) return;

            using (Brush b = new SolidBrush(Theme.Window))
                g.FillRectangle(b, box);
            using (Pen p = new Pen(Theme.Line))
                g.DrawRectangle(p, box);

            using (Pen p = new Pen(Color.FromArgb(0x26, 0x2C, 0x36)))
            {
                for (int i = 1; i < 4; i++)
                {
                    int x = box.Left + box.Width * i / 4;
                    int y = box.Top + box.Height * i / 4;
                    g.DrawLine(p, x, box.Top, x, box.Bottom);
                    g.DrawLine(p, box.Left, y, box.Right, y);
                }
            }

            if (_cal == null || !_cal.HasTravel)
            {
                using (Brush b = new SolidBrush(Theme.TextFaint))
                {
                    StringFormat sf = new StringFormat();
                    sf.Alignment = StringAlignment.Center;
                    sf.LineAlignment = StringAlignment.Center;
                    g.DrawString("capture the travel first", Theme.UiFontSmall, b, box, sf);
                }
                return;
            }

            int steps = Math.Max(24, box.Width);
            PointF[] pts = new PointF[steps + 1];
            for (int i = 0; i <= steps; i++)
            {
                double t = (double)i / steps;
                int raw = (int)Math.Round(_cal.Min + t * (_cal.Max - _cal.Min));
                double outv = _cal.Apply(raw);
                pts[i] = new PointF(
                    box.Left + (float)(t * box.Width),
                    box.Bottom - (float)(outv * box.Height));
            }

            using (Pen p = new Pen(Theme.Accent, 2f))
                g.DrawLines(p, pts);

            if (_hasLive)
            {
                double t = (_liveRaw - (double)_cal.Min) / (_cal.Max - _cal.Min);
                if (t >= 0.0 && t <= 1.0)
                {
                    float x = box.Left + (float)(t * box.Width);
                    float y = box.Bottom - (float)(_cal.Apply(_liveRaw) * box.Height);

                    using (Pen p = new Pen(Color.FromArgb(0x50, 0xE6, 0xE8, 0xEB)))
                    {
                        g.DrawLine(p, x, box.Top, x, box.Bottom);
                        g.DrawLine(p, box.Left, y, box.Right, y);
                    }
                    using (Brush b = new SolidBrush(Theme.Text))
                        g.FillEllipse(b, x - 4, y - 4, 8, 8);
                }
            }
        }
    }

    // Left hand navigation entry.
    public class NavButton : Control
    {
        private bool _selected;
        private bool _hover;

        public NavButton(string text)
        {
            Text = text;
            Height = 40;
            Cursor = Cursors.Hand;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer
                     | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
        }

        public bool Selected
        {
            get { return _selected; }
            set { _selected = value; Invalidate(); }
        }

        protected override void OnMouseEnter(EventArgs e)
        {
            _hover = true; Invalidate(); base.OnMouseEnter(e);
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            _hover = false; Invalidate(); base.OnMouseLeave(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            Color back = _selected ? Theme.Panel : (_hover ? Theme.PanelHi : Theme.Rail);
            using (Brush b = new SolidBrush(back))
                g.FillRectangle(b, ClientRectangle);

            if (_selected)
            {
                using (Brush b = new SolidBrush(Theme.Accent))
                    g.FillRectangle(b, new Rectangle(0, 0, 3, Height));
            }

            StringFormat sf = new StringFormat();
            sf.LineAlignment = StringAlignment.Center;
            using (Brush b = new SolidBrush(_selected ? Theme.Text : Theme.TextDim))
                g.DrawString(Text, _selected ? Theme.UiFontBold : Theme.UiFont, b,
                    new RectangleF(18, 0, Width - 20, Height), sf);
        }
    }
}
