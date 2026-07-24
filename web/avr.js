// Flashing an Arduino Uno from a web page, over WebSerial.
//
// The Uno's bootloader (optiboot) speaks STK500v1, which is simple enough to
// implement directly: sync, enter programming mode, load an address, write a
// page, repeat, leave. No server, no driver, no install — the browser opens the
// USB serial port the visitor picks and talks to the bootloader itself.
//
// A Mega 2560 speaks STK500v2 and an ESP8266 uses a different protocol again,
// so this file is Uno-only on purpose.

export const STK = {
  OK: 0x10, INSYNC: 0x14, CRC_EOP: 0x20,
  GET_SYNC: 0x30, ENTER_PROGMODE: 0x50, LEAVE_PROGMODE: 0x51,
  LOAD_ADDRESS: 0x55, PROG_PAGE: 0x64, READ_SIGN: 0x75,
};

const PAGE_SIZE = 128;          // atmega328p flash page, in bytes
const sleep = ms => new Promise(r => setTimeout(r, ms));

// ---- Intel HEX -----------------------------------------------------------

/** Turn an Intel HEX file into a flat byte array starting at address 0. */
export function parseHex(text) {
  const bytes = [];
  let maxAddr = 0;
  let base = 0;

  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line[0] !== ':') continue;

    const len  = parseInt(line.substr(1, 2), 16);
    const addr = parseInt(line.substr(3, 4), 16);
    const type = parseInt(line.substr(7, 2), 16);

    // Checksum: the length, address, type, data and checksum bytes must sum to
    // zero in the low byte. A corrupted download has to fail here, loudly,
    // rather than halfway through writing it to a chip.
    //
    // Check the line is long enough first. Reading past the end gives
    // parseInt('') === NaN, and NaN & 0xff is 0 — so a truncated or
    // length-corrupted line would sail through the checksum untouched.
    const need = 1 + (len + 5) * 2;
    if (line.length < need)
      throw new Error(`truncated line in hex file near address ${addr} ` +
                      `(needs ${need} characters, has ${line.length})`);

    let sum = 0;
    for (let i = 1; i < need; i += 2) {
      const byte = parseInt(line.substr(i, 2), 16);
      if (Number.isNaN(byte)) throw new Error(`bad character in hex file near address ${addr}`);
      sum += byte;
    }
    if ((sum & 0xff) !== 0) throw new Error(`bad checksum in hex file near address ${addr}`);

    if (type === 0x01) break;                       // end of file
    if (type === 0x04) {                            // extended linear address
      base = parseInt(line.substr(9, 4), 16) << 16;
      continue;
    }
    if (type !== 0x00) continue;

    for (let i = 0; i < len; i++) {
      const a = base + addr + i;
      bytes[a] = parseInt(line.substr(9 + i * 2, 2), 16);
      if (a > maxAddr) maxAddr = a;
    }
  }

  const out = new Uint8Array(maxAddr + 1);
  for (let i = 0; i <= maxAddr; i++) out[i] = bytes[i] ?? 0xff;   // unwritten = erased
  return out;
}

// ---- a serial port you can await ----------------------------------------

class Wire {
  constructor(port) {
    this.port = port;
    this.buf = [];
    this.reader = port.readable.getReader();
    this.writer = port.writable.getWriter();
    this.dead = false;
    this._pump();
  }

  async _pump() {
    try {
      while (true) {
        const { value, done } = await this.reader.read();
        if (done) break;
        if (value) this.buf.push(...value);
      }
    } catch { /* port closed under us */ }
    this.dead = true;
  }

  async write(bytes) { await this.writer.write(new Uint8Array(bytes)); }

  /** Wait for exactly n bytes, or give up. */
  async read(n, timeout = 1200) {
    const until = Date.now() + timeout;
    while (this.buf.length < n) {
      if (Date.now() > until) throw new Error(`board went quiet (wanted ${n} bytes, got ${this.buf.length})`);
      await sleep(5);
    }
    return this.buf.splice(0, n);
  }

  flush() { this.buf.length = 0; }

  async close() {
    try { await this.reader.cancel(); } catch {}
    try { this.reader.releaseLock(); } catch {}
    try { this.writer.releaseLock(); } catch {}
  }
}

// ---- the protocol --------------------------------------------------------

async function cmd(wire, bytes, timeout) {
  wire.flush();
  await wire.write([...bytes, STK.CRC_EOP]);
  const [a, b] = await wire.read(2, timeout);
  if (a !== STK.INSYNC || b !== STK.OK)
    throw new Error(`board answered 0x${a.toString(16)} 0x${b.toString(16)}, expected in-sync/ok`);
}

/** Pulse DTR/RTS so the bootloader runs. This is the auto-reset every Uno has. */
async function resetBoard(port) {
  await port.setSignals({ dataTerminalReady: false, requestToSend: false });
  await sleep(250);
  await port.setSignals({ dataTerminalReady: true, requestToSend: true });
  await sleep(50);
}

/**
 * Write a hex image to an Uno.
 * @param {SerialPort} port    already-open WebSerial port
 * @param {Uint8Array} image   flat flash image from parseHex
 * @param {(pct:number, note:string)=>void} onProgress
 */
export async function flashUno(port, image, onProgress = () => {}) {
  const wire = new Wire(port);
  try {
    onProgress(0, 'Resetting the board…');
    await resetBoard(port);
    await sleep(120);

    // Optiboot only listens for a moment after reset, so sync fast and retry.
    onProgress(3, 'Saying hello to the bootloader…');
    let synced = false;
    for (let i = 0; i < 12 && !synced; i++) {
      try { await cmd(wire, [STK.GET_SYNC], 300); synced = true; }
      catch { await sleep(60); }
    }
    if (!synced) throw new Error(
      'The bootloader did not answer. Unplug the board, plug it back in, and try again — ' +
      'and make sure nothing else (Arduino IDE, a serial monitor) has the port open.');

    onProgress(6, 'Entering programming mode…');
    await cmd(wire, [STK.ENTER_PROGMODE]);

    const total = image.length;
    for (let addr = 0; addr < total; addr += PAGE_SIZE) {
      const page = image.slice(addr, Math.min(addr + PAGE_SIZE, total));
      const wordAddr = addr >> 1;                       // STK500 addresses words
      await cmd(wire, [STK.LOAD_ADDRESS, wordAddr & 0xff, (wordAddr >> 8) & 0xff]);
      await cmd(wire, [STK.PROG_PAGE, (page.length >> 8) & 0xff, page.length & 0xff, 0x46, ...page], 2000);
      onProgress(6 + Math.round((addr / total) * 90), `Writing… ${Math.round((addr / total) * 100)}%`);
    }

    onProgress(97, 'Leaving programming mode…');
    await cmd(wire, [STK.LEAVE_PROGMODE]);
    onProgress(100, 'Done — your Arduino is running the new code.');
  } finally {
    await wire.close();
  }
}
