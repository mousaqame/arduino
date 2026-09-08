using System;
using System.Windows.Forms;
using WheelForge.Ui;

namespace WheelForge
{
    internal static class Program
    {
        [STAThread]
        private static void Main(string[] argv)
        {
            // --page <monitor|calibration|firmware|modes|about>  open straight there
            // --connect                                         connect on launch
            string page = "monitor";
            bool autoConnect = false;

            for (int i = 0; i < argv.Length; i++)
            {
                if (argv[i] == "--page" && i + 1 < argv.Length) page = argv[++i].ToLowerInvariant();
                else if (argv[i] == "--connect") autoConnect = true;
            }

            // Held for the life of the process so the installer can see that
            // WheelForge is running. Without it Setup deletes the install
            // folder out from under a running copy and then cannot replace the
            // locked executable, leaving a half-removed install behind.
            bool createdNew;
            using (System.Threading.Mutex running =
                       new System.Threading.Mutex(true, "WheelForgeRunning", out createdNew))
            {
                Run(page, autoConnect);
                GC.KeepAlive(running);
            }
        }

        private static void Run(string page, bool autoConnect)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            // A crash in a HID callback should say what happened rather than
            // vanishing the window.
            Application.ThreadException += delegate(object s, System.Threading.ThreadExceptionEventArgs e)
            {
                Report(e.Exception);
            };
            AppDomain.CurrentDomain.UnhandledException += delegate(object s, UnhandledExceptionEventArgs e)
            {
                Report(e.ExceptionObject as Exception);
            };

            Application.Run(new MainForm(page, autoConnect));
        }

        private static void Report(Exception ex)
        {
            string text = ex == null ? "Unknown error." : ex.ToString();
            MessageBox.Show(text, "WheelForge hit an error",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }
}
