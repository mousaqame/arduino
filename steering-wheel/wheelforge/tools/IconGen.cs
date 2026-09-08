using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;

// Draws the WheelForge icon and packs it into a multi-size .ico.
//
// Generated rather than checked in as a binary, so the icon is editable in the
// same place as everything else. build.ps1 runs this when the .ico is missing.
internal static class IconGen
{
    private static readonly int[] Sizes = { 16, 24, 32, 48, 64, 128, 256 };

    private static int Main(string[] argv)
    {
        string outPath = argv.Length > 0 ? argv[0] : "wheelforge.ico";

        // Sizes below 256 are stored as uncompressed DIBs. PNG-compressed
        // entries are legal since Vista and Explorer reads them, but the legacy
        // System.Drawing.Icon class cannot, and neither can some resource
        // compilers -- so only the 256 entry, where the size saving actually
        // matters, is PNG.
        byte[][] blobs = new byte[Sizes.Length][];
        for (int i = 0; i < Sizes.Length; i++)
        {
            using (Bitmap bmp = Draw(Sizes[i]))
            {
                if (Sizes[i] >= 256)
                {
                    using (MemoryStream ms = new MemoryStream())
                    {
                        bmp.Save(ms, ImageFormat.Png);
                        blobs[i] = ms.ToArray();
                    }
                }
                else
                {
                    blobs[i] = DibBytes(bmp);
                }
            }
        }

        using (FileStream fs = new FileStream(outPath, FileMode.Create, FileAccess.Write))
        using (BinaryWriter w = new BinaryWriter(fs))
        {
            // ICONDIR
            w.Write((ushort)0);                 // reserved
            w.Write((ushort)1);                 // type: icon
            w.Write((ushort)Sizes.Length);

            int offset = 6 + 16 * Sizes.Length;
            for (int i = 0; i < Sizes.Length; i++)
            {
                int s = Sizes[i];
                w.Write((byte)(s >= 256 ? 0 : s));   // 0 means 256
                w.Write((byte)(s >= 256 ? 0 : s));
                w.Write((byte)0);                    // palette size
                w.Write((byte)0);                    // reserved
                w.Write((ushort)1);                  // colour planes
                w.Write((ushort)32);                 // bits per pixel
                w.Write((uint)blobs[i].Length);
                w.Write((uint)offset);
                offset += blobs[i].Length;
            }

            for (int i = 0; i < Sizes.Length; i++)
                w.Write(blobs[i]);
        }

        Console.WriteLine("wrote " + outPath + " with " + Sizes.Length + " sizes");
        return 0;
    }

    // An icon DIB is a BITMAPINFOHEADER whose height is doubled, followed by the
    // colour rows bottom-up and then a 1bpp AND mask. With 32bpp colour the
    // alpha channel does the masking, so the mask itself is left all zero.
    private static byte[] DibBytes(Bitmap bmp)
    {
        int w = bmp.Width, h = bmp.Height;
        int maskStride = ((w + 31) / 32) * 4;

        using (MemoryStream ms = new MemoryStream())
        using (BinaryWriter bw = new BinaryWriter(ms))
        {
            bw.Write(40);                       // biSize
            bw.Write(w);                        // biWidth
            bw.Write(h * 2);                    // biHeight: colour + mask
            bw.Write((ushort)1);                // biPlanes
            bw.Write((ushort)32);               // biBitCount
            bw.Write(0);                        // biCompression: BI_RGB
            bw.Write(w * h * 4 + maskStride * h);
            bw.Write(0); bw.Write(0);           // pixels per metre
            bw.Write(0); bw.Write(0);           // palette

            for (int y = h - 1; y >= 0; y--)    // bottom-up
            {
                for (int x = 0; x < w; x++)
                {
                    Color c = bmp.GetPixel(x, y);
                    bw.Write(c.B);
                    bw.Write(c.G);
                    bw.Write(c.R);
                    bw.Write(c.A);
                }
            }

            bw.Write(new byte[maskStride * h]);
            return ms.ToArray();
        }
    }

    private static Bitmap Draw(int size)
    {
        Bitmap bmp = new Bitmap(size, size, PixelFormat.Format32bppArgb);

        using (Graphics g = Graphics.FromImage(bmp))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Color.Transparent);

            Color amber = Color.FromArgb(0xE8, 0xA3, 0x3D);
            Color dark = Color.FromArgb(0x1C, 0x20, 0x27);

            float s = size;
            float cx = s / 2f, cy = s / 2f;

            // Dark rounded plate behind the wheel, so the icon reads on light
            // and dark taskbars alike.
            float pad = s * 0.02f;
            using (Brush b = new SolidBrush(dark))
            using (GraphicsPath path = RoundedRect(pad, pad, s - pad * 2, s - pad * 2, s * 0.22f))
                g.FillPath(b, path);

            float rimWidth = Math.Max(1.5f, s * 0.085f);
            float radius = s * 0.335f;

            using (Pen p = new Pen(amber, rimWidth))
                g.DrawEllipse(p, cx - radius, cy - radius, radius * 2, radius * 2);

            // Three spokes: left, right, and down. The classic racing wheel.
            float hub = s * 0.10f;
            float spoke = Math.Max(1.2f, s * 0.072f);
            using (Pen p = new Pen(amber, spoke))
            {
                p.StartCap = LineCap.Round;
                p.EndCap = LineCap.Round;
                g.DrawLine(p, cx - hub * 0.5f, cy, cx - radius + rimWidth * 0.4f, cy);
                g.DrawLine(p, cx + hub * 0.5f, cy, cx + radius - rimWidth * 0.4f, cy);
                g.DrawLine(p, cx, cy + hub * 0.5f, cx, cy + radius - rimWidth * 0.4f);
            }

            using (Brush b = new SolidBrush(amber))
                g.FillEllipse(b, cx - hub, cy - hub, hub * 2, hub * 2);
        }

        return bmp;
    }

    private static GraphicsPath RoundedRect(float x, float y, float w, float h, float r)
    {
        GraphicsPath p = new GraphicsPath();
        if (r <= 0.5f) { p.AddRectangle(new RectangleF(x, y, w, h)); return p; }

        float d = r * 2;
        p.AddArc(x, y, d, d, 180, 90);
        p.AddArc(x + w - d, y, d, d, 270, 90);
        p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
        p.AddArc(x, y + h - d, d, d, 90, 90);
        p.CloseFigure();
        return p;
    }
}
