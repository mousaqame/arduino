using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Windows.Forms;
using WheelForge.Core;

namespace WheelForge.Ui
{
    // Picks a board, finds its image and the right tool, and flashes it.
    //
    // The tools are not bundled: avrdude arrives with the Arduino IDE or
    // arduino-cli, esptool with pip, and an RP2040 needs no tool at all. The
    // page says which one it is about to use and where it found it, because a
    // flash that goes wrong is much easier to unpick when you know that.
    public class FirmwarePage : Panel
    {
        private ComboBox _boardBox;
        private ComboBox _portBox;
        private Button _refreshPorts;
        private TextBox _imageBox;
        private Button _browseButton;
        private Button _flashButton;
        private Label _status;
        private Label _toolsLabel;
        private TextBox _log;

        private Thread _worker;

        // Set by MainForm. WheelForge holding a port open in its own Wheel test
        // page is the most likely reason a flash cannot get at it, and blaming
        // the user for that would be poor manners.
        public Action<string> ReleasePort;

        public FirmwarePage()
        {
            BackColor = Theme.Window;
            Build();
            RefreshPorts();
            OnBoardChanged();
            ReportTools();
        }

        // =====================================================================

        private void Build()
        {
            TableLayoutPanel grid = new TableLayoutPanel();
            grid.Dock = DockStyle.Fill;
            grid.BackColor = Theme.Window;
            grid.ColumnCount = 1;
            grid.RowCount = 2;
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 244));
            grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));

            Card top = new Card("Flash firmware");
            top.Dock = DockStyle.Fill;
            top.Margin = new Padding(0, 0, 0, 10);

            // Children placed at absolute coordinates do not go through the
            // card's DisplayRectangle, so the title strip has to be stepped
            // over by hand or the first row lands on top of the heading.
            int y = 46;

            top.Controls.Add(Theme.MakeLabel("Board", Theme.UiFont, Theme.TextDim, 4, y + 4));
            _boardBox = new ComboBox();
            _boardBox.DropDownStyle = ComboBoxStyle.DropDownList;
            _boardBox.SetBounds(96, y, 320, 24);
            _boardBox.BackColor = Theme.PanelHi;
            _boardBox.ForeColor = Theme.Text;
            _boardBox.FlatStyle = FlatStyle.Flat;
            foreach (BoardProfile b in BoardCatalog.Flashable())
                _boardBox.Items.Add(b.Name);
            _boardBox.SelectedIndexChanged += delegate { OnBoardChanged(); };
            top.Controls.Add(_boardBox);
            // Selecting a board is deliberately left until the end of Build:
            // it raises SelectedIndexChanged, and OnBoardChanged touches
            // controls that do not exist yet at this point.
            y += 32;

            top.Controls.Add(Theme.MakeLabel("Port", Theme.UiFont, Theme.TextDim, 4, y + 4));
            _portBox = new ComboBox();
            _portBox.DropDownStyle = ComboBoxStyle.DropDownList;
            _portBox.SetBounds(96, y, 160, 24);
            _portBox.BackColor = Theme.PanelHi;
            _portBox.ForeColor = Theme.Text;
            _portBox.FlatStyle = FlatStyle.Flat;
            top.Controls.Add(_portBox);

            _refreshPorts = new Button();
            _refreshPorts.Text = "Refresh";
            _refreshPorts.SetBounds(264, y - 1, 76, 26);
            _refreshPorts.Click += delegate { RefreshPorts(); };
            Theme.StyleButton(_refreshPorts);
            top.Controls.Add(_refreshPorts);
            y += 32;

            top.Controls.Add(Theme.MakeLabel("Image", Theme.UiFont, Theme.TextDim, 4, y + 4));
            _imageBox = new TextBox();
            _imageBox.SetBounds(96, y, 480, 22);
            _imageBox.BackColor = Theme.PanelHi;
            _imageBox.ForeColor = Theme.Text;
            _imageBox.BorderStyle = BorderStyle.FixedSingle;
            top.Controls.Add(_imageBox);

            _browseButton = new Button();
            _browseButton.Text = "Browse";
            _browseButton.SetBounds(584, y - 2, 76, 26);
            _browseButton.Click += delegate { Browse(); };
            Theme.StyleButton(_browseButton);
            top.Controls.Add(_browseButton);
            y += 34;

            _flashButton = new Button();
            _flashButton.Text = "Flash";
            _flashButton.SetBounds(96, y, 140, 30);
            _flashButton.Click += delegate { StartFlash(); };
            Theme.StylePrimary(_flashButton);
            top.Controls.Add(_flashButton);

            _status = Theme.MakeLabel("", Theme.UiFontSmall, Theme.TextDim, 248, y + 8);
            top.Controls.Add(_status);
            y += 38;

            _toolsLabel = Theme.MakeLabel("", Theme.MonoFontSmall, Theme.TextFaint, 4, y);
            top.Controls.Add(_toolsLabel);

            grid.Controls.Add(top, 0, 0);

            Card logCard = new Card("Output");
            logCard.Dock = DockStyle.Fill;

            _log = new TextBox();
            _log.Dock = DockStyle.Fill;
            _log.Multiline = true;
            _log.ReadOnly = true;
            _log.ScrollBars = ScrollBars.Both;
            _log.WordWrap = false;
            _log.BackColor = Theme.Window;
            _log.ForeColor = Theme.TextDim;
            _log.Font = Theme.MonoFontSmall;
            _log.BorderStyle = BorderStyle.None;
            _log.Text = "Nothing flashed yet.";
            logCard.Controls.Add(_log);

            grid.Controls.Add(logCard, 0, 1);
            Controls.Add(grid);

            if (_boardBox.Items.Count > 0) _boardBox.SelectedIndex = 0;
        }

        // Fills in a board and port picked somewhere else, so arriving here from
        // the device list lands ready to flash rather than ready to guess.
        public void Preselect(string boardId, string portName)
        {
            RefreshPorts();

            if (!string.IsNullOrEmpty(boardId))
            {
                List<BoardProfile> boards = BoardCatalog.Flashable();
                for (int i = 0; i < boards.Count; i++)
                {
                    if (boards[i].Id != boardId) continue;
                    if (i < _boardBox.Items.Count) _boardBox.SelectedIndex = i;
                    break;
                }
            }

            if (!string.IsNullOrEmpty(portName) && _portBox.Items.Contains(portName))
                _portBox.SelectedItem = portName;
        }

        private void ReportTools()
        {
            string avrdude = ToolLocator.FindAvrdude();
            string esptool = ToolLocator.FindEsptool();

            string text = "avrdude: " + (avrdude != null ? avrdude : "not found")
                        + "\r\nesptool: " + (esptool != null ? esptool : "not found  (pip install esptool)");

            _toolsLabel.Text = text;
            _toolsLabel.MaximumSize = new Size(900, 0);
        }

        // =====================================================================

        private BoardProfile SelectedBoard()
        {
            int i = _boardBox.SelectedIndex;
            List<BoardProfile> boards = BoardCatalog.Flashable();
            if (i < 0 || i >= boards.Count) return null;
            return boards[i];
        }

        private void OnBoardChanged()
        {
            // Guard as well as ordering the build: a combo raises this the
            // moment its selection is set, and there is no useful work to do
            // before the controls it writes to exist.
            if (_imageBox == null || _flashButton == null || _status == null) return;

            BoardProfile board = SelectedBoard();
            if (board == null) return;

            string image = ResolveImage(board);
            _imageBox.Text = image != null ? image : "";

            bool needsPort = board.Tool != FlashTool.Uf2Copy;
            _portBox.Enabled = needsPort;
            _refreshPorts.Enabled = needsPort;

            if (image == null)
            {
                SetStatus("No image found for this board. Run firmware/build.ps1.", Theme.Warn);
                _flashButton.Enabled = false;
            }
            else
            {
                string hint;
                if (board.Tool == FlashTool.Uf2Copy)
                    hint = "Hold BOOTSEL and plug the board in, then press Flash.";
                else if (board.Mode == LinkMode.Bridge)
                    hint = "Bridge board: after flashing, connect it on the Wheel Test page.";
                else
                    hint = "Ready.";
                SetStatus(hint, Theme.TextDim);
                _flashButton.Enabled = true;
            }
        }

        // Looks beside the executable first, so an installed copy uses the
        // images shipped with it, then back up the source tree for a dev build.
        private static string ResolveImage(BoardProfile board)
        {
            if (!board.HasImage) return null;

            string exeDir = Path.GetDirectoryName(Application.ExecutablePath);
            string[] candidates = new string[]
            {
                Path.Combine(exeDir, Path.Combine("firmware", board.ImageName)),
                Path.Combine(exeDir, Path.Combine(@"..\firmware\images", board.ImageName)),
                Path.Combine(exeDir, Path.Combine(@"..\..\firmware\images", board.ImageName))
            };

            foreach (string c in candidates)
            {
                try
                {
                    string full = Path.GetFullPath(c);
                    if (File.Exists(full)) return full;
                }
                catch (Exception)
                {
                    // A malformed candidate path is simply not a match.
                }
            }

            return null;
        }

        private void RefreshPorts()
        {
            string previous = _portBox.SelectedItem as string;
            _portBox.Items.Clear();

            foreach (string p in ToolLocator.SerialPorts())
                _portBox.Items.Add(p);

            if (previous != null && _portBox.Items.Contains(previous))
                _portBox.SelectedItem = previous;
            else if (_portBox.Items.Count > 0)
                _portBox.SelectedIndex = _portBox.Items.Count - 1;
        }

        private void Browse()
        {
            BoardProfile board = SelectedBoard();
            using (OpenFileDialog dlg = new OpenFileDialog())
            {
                dlg.Title = "Choose a firmware image";
                dlg.Filter = "Firmware images (*.hex;*.bin;*.uf2)|*.hex;*.bin;*.uf2|All files (*.*)|*.*";
                if (board != null && board.ImageExtension == ".uf2") dlg.FilterIndex = 1;
                if (dlg.ShowDialog(this) == DialogResult.OK) _imageBox.Text = dlg.FileName;
            }
        }

        private void SetStatus(string text, Color colour)
        {
            _status.Text = text;
            _status.ForeColor = colour;
        }

        // =====================================================================

        private void StartFlash()
        {
            if (_worker != null && _worker.IsAlive) return;

            BoardProfile board = SelectedBoard();
            if (board == null) return;

            string image = _imageBox.Text.Trim();
            string port = _portBox.SelectedItem as string;

            if (board.Tool == FlashTool.AvrdudeAvr109 || board.Tool == FlashTool.Avrdude
                || board.Tool == FlashTool.Esptool)
            {
                if (string.IsNullOrEmpty(port))
                {
                    SetStatus("Pick the COM port the board is on.", Theme.Bad);
                    return;
                }
            }

            // Flashing over a working wheel is the one genuinely destructive
            // thing this app can do, so it asks first and says what is at stake.
            string warning =
                "Flash " + Path.GetFileName(image) + " to " + board.Name + "?\r\n\r\n"
              + "This erases whatever firmware is on the board. If that board is "
              + "running EMC Lite, the wheel stops working until you flash EMC Lite "
              + "back with XLoader.\r\n\r\n"
              + "This firmware has not been tested on hardware yet.";

            if (MessageBox.Show(this, warning, "Flash firmware",
                    MessageBoxButtons.OKCancel, MessageBoxIcon.Warning,
                    MessageBoxDefaultButton.Button2) != DialogResult.OK)
                return;

            if (ReleasePort != null && !string.IsNullOrEmpty(port))
            {
                ReleasePort(port);
                Thread.Sleep(250);        // let Windows actually free the handle
            }

            _log.Text = "";
            _flashButton.Enabled = false;
            _boardBox.Enabled = false;
            SetStatus("Flashing...", Theme.Warn);

            _worker = new Thread(delegate ()
            {
                Flasher flasher = new Flasher(AppendLog);
                FlashResult result = flasher.Flash(board, port, image);
                FlashFinished(result);
            });
            _worker.IsBackground = true;
            _worker.Name = "WheelForge flasher";
            _worker.Start();
        }

        // Called from the flasher thread.
        private void AppendLog(string line)
        {
            if (IsDisposed || !IsHandleCreated) return;
            try
            {
                BeginInvoke((MethodInvoker)delegate
                {
                    _log.AppendText(line + "\r\n");
                });
            }
            catch (InvalidOperationException)
            {
                // Window closed while the tool was still talking.
            }
        }

        private void FlashFinished(FlashResult result)
        {
            if (IsDisposed || !IsHandleCreated) return;
            try
            {
                BeginInvoke((MethodInvoker)delegate
                {
                    _flashButton.Enabled = true;
                    _boardBox.Enabled = true;
                    SetStatus(result.Message, result.Success ? Theme.Good : Theme.Bad);
                    RefreshPorts();
                });
            }
            catch (InvalidOperationException)
            {
            }
        }
    }
}
